// RUN: %check_clang_tidy -std=c++23-or-later %s malterlib-coroutine-actor-reference %t -- -config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" -- -I %S/Inputs

#include "malterlib-actor-phase.h"

// What an expression is relative to the object of the actor decides whether
// what a callback of fg_OnResume does to it can remove what a lookup refers
// to: the collection itself, also through a cast, a conditional, a comma, a
// local reference or pointer to it, a pointer to the object, a member of a
// member, or an accessor that gives it, has its entries removed by a removing
// call, a move, a delete or an assignment; an entry of it, by subscript, a
// member function giving a reference or a pointer, a local reference or
// pointer to one, or a local iterator, only by a removing call or a delete;
// what changes a collection in it, also a move or a replacement of one that
// holds one, changes what is looked up in that collection, not the entry,
// and an update of its data neither.
// Each case looks up `pEntry` in the callback, does what it does, and uses
// `pEntry` after a suspension: reported when what it does can remove it.
// The same classification tells what a lookup points into: one through any
// of those forms is into the member it goes through first, which removing
// from another member leaves.
// This file is generated.

namespace std {
template <typename t_CType, decltype(sizeof(0)) t_Size> struct array {
  t_CType &operator[](decltype(sizeof(0)) _Index);
  t_CType &at(decltype(sizeof(0)) _Index);
  t_CType &front();
  t_CType &back();
  t_CType *data();
  t_CType *begin();
  t_CType *end();
};
template <typename t_CType, decltype(sizeof(0)) t_Size>
t_CType *begin(array<t_CType, t_Size> &_Array);
} // namespace std

extern "C" void free(void *);
namespace std {
class type_info;
}

namespace NRelations {
TCFuture<int> fg_Work();
struct CTagged;

// An owning pointer.
template <typename t_CType> struct TCOwner {
  t_CType *operator->() const;
  t_CType &operator*() const;
};
// One whose operators are inherited, one with only `operator*`, and one that
// can be cleared.
template <typename t_CType> struct TCOwnerBase {
  t_CType *operator->() const;
};
template <typename t_CType> struct TCDerivedOwner : TCOwnerBase<t_CType> {};
template <typename t_CType> struct TCStarOwner {
  t_CType &operator*() const;
};
template <typename t_CType> struct TCClearableOwner {
  t_CType *operator->() const;
  void f_Clear();
};

// Types that own themselves, also through each other, which what is told of
// them ends at.
struct CNode {
  int m_Value;
  TCOwner<CNode> m_pNext;
};
struct CRingB;
struct CRingA {
  int m_Value;
  TCOwner<CRingB> m_pB;
};
struct CRingB {
  TCOwner<CRingA> m_pA;
};
template <typename t_CEntry> struct TCEntries {
  t_CEntry *f_FindEqual(int _Key);
  t_CEntry &operator[](int _Key);
};
void fg_ChangeNode(CNode &_Node);
void fg_ChangeRing(CRingA &_Ring);

// A collection in an entry.
struct CChildren {
  int *f_FindEqual(int _Key);
  bool f_Remove(int _Key);
  void f_Clear();
  void f_Tidy() { f_Clear(); }
  void f_Reserve(int _Count);
  // Its own state.
  void f_ResetCount() { m_Count = 0; }
  int m_Count = 0;
};

struct CEntry;
void fg_ChangeEntryPointer(CEntry *_pEntry);
// What an entry owns.
struct CHeapData {
  int m_Value;
};
struct CEntry {
  int m_Value;
  TCOwner<CHeapData> m_pData;
  CHeapData m_Inline;
  // Give what it owns, the owning pointer, and a collection or an entry in
  // it.
  TCOwner<CHeapData> &f_Owner() { return m_pData; }
  CHeapData *f_Data() { return m_pData.operator->(); }
  int *f_FirstChild() { return m_Children.f_FindEqual(0); }
  CEntry *m_pNext;
  CChildren m_Children;
  std::array<int, 2> m_Fixed;
  // Changes data of the entry, not a collection.
  void f_Update() {
    m_Value = 1;
    m_pNext = nullptr;
  }
  // Replaces the entry, also the collection in it.
  void f_Renew() { *this = CEntry{}; }
  // Assigns a collection in the entry.
  void f_ResetChildren() { m_Children = CChildren{}; }
  // Moves the entry out, which moves from the collection it holds.
  void f_MoveOut() { (void)fg_Move(*this); }
  // Gives a pointer to itself on to what can change it.
  void f_ChangeThroughPointer() { fg_ChangeEntryPointer(this); }
  // Gives the collection it holds.
  CChildren &f_Children() { return m_Children; }
};

// A view of entries, which refers back to the collection it is made from.
struct CView {
  CEntry *m_pFirst;
  bool f_Remove(int _Key);
};

// A view that refers back by a reference member.
struct CRefView {
  CEntry &m_First;
  bool f_Remove(int _Key);
};

struct CIterator {
  CEntry &operator*() const;
  CEntry *operator->() const;
  void f_Remove();
  // Changes the iterator, not the collection it was made from.
  void f_Next() { *this = CIterator{}; }
  // Another iterator at the same entry, a copy.
  CIterator f_GetIterator() const;
  CIterator &operator++();
};

struct CItemsGuardOwn;
struct CItemsGuardRef;
struct CItemsGuardUnknown;
struct CItems;
inline void fg_TouchItems(int, CItems &) {}
// A value all its own, which keeps nothing of what made it.
struct CDescription {
  int m_Value;
  ~CDescription();
};
// One that owns a buffer, and a collection of numbers.
struct CPrinted {
  char *m_pBuffer;
  ~CPrinted();
};
template <typename t_CValue> struct TCValues {
  t_CValue *m_pData;
  TCValues();
  TCValues(TCValues const &_Other);
  TCValues(TCValues &_Other);
  t_CValue &operator[](int _Index);
  void f_Clear();
  ~TCValues();
};
// A borrowing wrapper template, named like a collection, which only refers.
template <typename t_CItems> struct TCBorrowView {
  t_CItems &m_rItems;
  void f_Clear() { m_rItems.f_Clear(); }
};
// A view that borrows the collection and owns a scratch collection.
struct CScratchView {
  CItems *m_pItems;
  TCValues<int> m_Scratch;
  void f_Clear();
};
// A view that clears what it refers to when it goes.
struct CClearingView {
  CItems *m_pItems;
  ~CClearingView();
};
struct CItems {
  // Views made of it, also through local aliases and copies, or of nothing.
  CClearingView f_ViewAlias() {
    auto &Items = *this;
    return CClearingView{&Items};
  }
  CClearingView f_ViewPointerAlias() {
    auto *pItems = this;
    return CClearingView{pItems};
  }
  CClearingView f_ViewLocal() {
    CClearingView View{this};
    return View;
  }
  CClearingView f_ViewCopy() {
    CClearingView View{this};
    CClearingView Copy = View;
    return Copy;
  }
  CClearingView f_ViewOwn() { return CClearingView{nullptr}; }
  TCBorrowView<CItems> f_BorrowView() { return {*this}; }
  CScratchView f_ScratchView() { return {this, {}}; }
  // An independent value made of it.
  CDescription f_Description() const { return CDescription{}; }
  CPrinted f_Print() const { return CPrinted{nullptr}; }
  TCValues<int> f_Values() const;
  // Guards made by a member function of it, which keep it.
  CItemsGuardOwn f_GuardOwn();
  CItemsGuardRef f_GuardRef();
  CItemsGuardUnknown f_GuardUnknown();
  // A generic lambda whose pattern's call isn't resolved, which changes
  // nothing as instantiated.
  void f_GenericTouch() {
    auto fTouch = [this]<typename t_CValue>(t_CValue _Value) {
      fg_TouchItems(_Value, *this);
    };
    fTouch(0);
  }
  // What can be the collection itself or another.
  CItems &f_Select(bool _bOwn);
  // A view an operator gives.
  CView operator+(int _Offset);
  CEntry *f_FindEqual(int _Key);
  CEntry &operator[](int _Key);
  CEntry &f_Get(int _Key);
  bool f_Remove(int _Key);
  void f_Clear();
  void f_Touch();
  CIterator f_GetIterator();
  // A copy of it, whose entries are its own, and a view of it, which isn't.
  CItems f_Copy() const;
  CView f_View();
  CRefView f_RefView();
  CTagged f_GetSelf();
  // Assigned through a pointer to its assignment operator.
  void f_AssignPointed() {
    auto fAssign =
        static_cast<CItems &(CItems::*)(CItems &&)>(&CItems::operator=);
    (this->*fAssign)(CItems{});
  }
  // A const one whose body changes it, and one whose body can't be seen.
  void f_TidyConst() const { const_cast<CItems *>(this)->f_Clear(); }
  int f_CountConst() const;
  // Through a pointer to a member function that a lambda points elsewhere.
  void f_ClearCaptured() {
    auto fCall = &CItems::f_Touch;
    [this, fCall]() mutable {
      fCall = &CItems::f_Clear;
      (this->*fCall)();
    }();
  }
  // Through `this`, also dereferenced, cast, or a local pointer to it.
  void f_PruneDeref() { (*this).f_Remove(0); }
  void f_PruneCast() { static_cast<CItems &>(*this).f_Remove(0); }
  void f_PrunePointer() {
    auto *pSelf = this;
    pSelf->f_Remove(0);
  }
  // Not an update of an entry of it, nor a step of an iterator into it.
  void f_UpdateFirst() { (*this)[0].f_Update(); }
  void f_Step() {
    auto iEntry = f_GetIterator();
    iEntry.f_Next();
  }
  // An accessor that gives the collection itself, with a body or without.
  CItems &f_Entries() { return *this; }
  CItems &f_Self();
  void f_Prune() { f_Remove(0); }
  // Through a pointer to a member function: the one it points to, by name,
  // or one that can't be seen.
  void f_ClearPointed() {
    auto fClear = &CItems::f_Clear;
    (this->*fClear)();
  }
  void f_TouchPointed() {
    auto fTouch = &CItems::f_Touch;
    (this->*fTouch)();
  }
  void f_CallPointed(void (CItems::*_fCall)()) { (this->*_fCall)(); }
  // Accessors that give the collection itself, as an operator or with an
  // explicit object parameter.
  CItems &operator()() { return *this; }
  // Accessors that give a pointer or a reference to it otherwise.
  CItems *f_SelfAddress() { return &*this; }
  CItems *f_SelfAlias() {
    auto *pSelf = this;
    return pSelf;
  }
  CItems *f_SelfExplicitAddress(this CItems &_Self) { return &_Self; }
  CItems &f_SelfReference() {
    auto &Self = *this;
    return Self;
  }
  // Gives an entry, and an entry of a collection in one.
  CEntry &f_EntryAt(int _Key) { return (*this)[_Key]; }
  int *f_Grandchild() { return (*this)[0].m_Children.f_FindEqual(0); }
  // Consumed through a pointer to a member function qualified `&&`.
  void f_Drain() &&;
  // Calls through a cast that picks the overload, which changes nothing.
  void f_TouchCast() {
    (this->*static_cast<void (CItems::*)()>(&CItems::f_Touch))();
  }
  // Changes only what an entry of it holds.
  void f_TidyNested() { (*this)[0].m_Children.f_Clear(); }
  // Moves a number out.
  int f_CountMoved() { return fg_Move(m_Count); }
  // What isn't evaluated, which changes nothing.
  void f_ConstantProbe() { (void)__builtin_constant_p(&m_Count); }
  void f_ObjectSizeProbe() { (void)__builtin_object_size(&m_Count, 0); }
  void f_ClassifyProbe() { (void)__builtin_classify_type(m_Count = 0); }
  void f_SizeProbe() { (void)sizeof((f_Clear(), 0)); }
  void f_AlignProbe() { (void)alignof(decltype(f_Clear(), 0)); }
  void f_NoexceptProbe() { (void)noexcept(f_Clear()); }
  void f_TypeidProbe() { (void)typeid((f_Clear(), 0)); }
  int m_Count = 0;
  CItems &f_ExplicitSelf(this CItems &_Self) { return _Self; }
  CItems &f_ExplicitSelfDeclared(this CItems &_Self);
  // The collection itself as an rvalue, with a body and without.
  CItems &&f_Rvalue() { return static_cast<CItems &&>(*this); }
  CItems &&f_RvalueDeclared();
  // What consumes it, with an explicit object parameter or as an operator.
  void f_Consume(this CItems &&_Self);
  bool operator!() &&;
};

struct CWrapped {
  CItems m_Items;
};

// One whose virtual members can be overridden: a lookup told by name, any
// other an unknown change, whatever the body seen, unless it is called on
// what can only be of the class, like a member.
struct CVirtualItems {
  virtual CEntry *f_FindEqual(int _Key);
  virtual void f_Visit() {}
  virtual int f_Count() const { return 0; }
};

// One whose entry lives in a union, which assigning another member of ends.
struct CUnionItems {
  CUnionItems();
  ~CUnionItems();
  CEntry *f_FindEqual(int _Key);
  union {
    CEntry m_Entry;
    int m_Empty;
    TCOwner<CItems> m_pOwnedItems;
    int *m_pSlot;
  };
  int m_Count = 0;
  CEntry *m_pFirst = nullptr;
  // Updates the data of an element through a pointer it holds.
  void f_TouchFirst() { m_pFirst[0].m_Value = 1; }
};

// An entry with a union in it, and a collection of them.
struct CUnionEntry {
  int m_Value;
  union {
    int m_Empty;
    float m_Other;
  };
  // Writes a member of the union through what a helper gives back of it.
  void f_WriteThroughAlias();
  // Writes a member of the union through a pointer pointed at each.
  void f_WriteUnion() {
    int *pSlot = &m_Empty;
    pSlot = reinterpret_cast<int *>(&m_Other);
    *pSlot = 0;
  }
};
// One whose union is in a base.
struct CUnionBase {
  union {
    int m_Empty;
    float m_Other;
  };
};
struct CUnionDerived : CUnionBase {
  int m_Value;
};
struct CDerivedEntries {
  CUnionDerived *f_FindEqual(int _Key);
  CUnionDerived &operator[](int _Key);
};
// A pointer kept in bits, like a `TCBitStorePointer`.
struct CTagged {
  unsigned long long m_Bits : 48;
  CItems *operator->() const;
};
// A collection of values copied out by subscript.
struct CPlain {
  CChildren m_Children;
};
struct CValueItems {
  CEntry *f_FindEqual(int _Key);
  CPlain operator[](int _Key);
};
// A static call operator, given the collection.
struct CRemover {
  static void operator()(CItems &_Items, int _Key) { _Items.f_Remove(_Key); }
};
extern CItems g_OtherItems;
inline CItems &CItems::f_Select(bool _bOwn) {
  return _bOwn ? *this : g_OtherItems;
}
struct CUnionEntries {
  CUnionEntry *f_FindEqual(int _Key);
  CUnionEntry &operator[](int _Key);
};

// A const call operator whose body can't be seen.
struct CConstCall {
  CEntry *f_FindEqual(int _Key);
  void operator()() const;
};

void fg_ClearIndirect(CItems **_ppItems);

// An entry that owns a collection through a pointer, and a collection of
// them.
struct COwningEntry {
  int m_Value;
  TCOwner<CItems> m_pItems;
};
struct COwningEntries {
  COwningEntry *f_FindEqual(int _Key);
  COwningEntry &operator[](int _Key);
};

// Entries that own what they point to, through inherited operators, only
// `operator*`, or what can be cleared, and plain data.
struct CHeapValue {
  int m_Value;
};
struct CInheritedOwningEntry {
  int m_Value;
  TCDerivedOwner<CItems> m_pItems;
};
struct CStarOwningEntry {
  int m_Value;
  TCStarOwner<CItems> m_pItems;
};
struct CHeapEntry {
  int m_Value;
  TCOwner<CHeapValue> m_pValue;
  TCClearableOwner<CHeapValue> m_pClearable;
};
void fg_ChangeInherited(CInheritedOwningEntry &_Entry);
void fg_ChangeStar(CStarOwningEntry &_Entry);
// An entry that removes itself by a name that removes.
struct CSelfRemoving {
  int m_Value;
  void f_Remove() { delete this; }
};
// An entry that holds a collection and has a virtual member function.
struct CVirtualEntry {
  int m_Value;
  CChildren m_Children;
  virtual void f_Alter() {}
};
// A collection whose subscript gives itself.
struct CSelfIndexed {
  CEntry *f_FindEqual(int _Key);
  CSelfIndexed &operator[](int) { return *this; }
  void f_Clear();
};
// What clears what it is given when it is consumed.
struct CWorker {
  void operator()(CItems &_Items) && { _Items.f_Clear(); }
};
void fg_ChangeUnionEntry(CUnionEntry &_Entry);

// What gives back what it is given, with a body.
template <typename t_CType> t_CType &fg_Same(t_CType &_Value) { return _Value; }
// Wraps an owning pointer.
struct CDataHolder {
  TCOwner<CHeapData> m_pData;
};
struct CHolderEntry {
  int m_Value;
  CDataHolder m_Holder;
};
// A union held by value in an entry.
struct CUnionState {
  union {
    int m_Int;
    float m_Float;
  };
};
struct CStateEntry {
  int m_Value;
  CUnionState m_State;
  void f_MoveOut() { (void)fg_Move(*this); }
};
void fg_ResetData(TCOwner<CHeapData> &_pData);
// An iterator given by a pointer.
struct CIteratorItems {
  CEntry *f_FindEqual(int _Key);
  CIterator *f_GetIteratorPointer();
};

// A collection whose `operator*`, `operator->` and `get()` give itself, in
// an entry.
struct CSelfDeref {
  int *f_FindEqual(int _Key);
  void f_Clear();
  CSelfDeref &operator*() { return *this; }
  CSelfDeref *operator->() { return this; }
  CSelfDeref *get() { return this; }
};
struct CSelfDerefEntry {
  int m_Value;
  CSelfDeref m_Children;
};
// What can't be seen given an entry or what an entry points to, which can
// end it.
void fg_DeleteEntry(CEntry *_pEntry);
void fg_DestroyEntry(CEntry &_Entry);
void fg_SetNumber(int &_Value);
// An owning pointer whose `operator->` is a member template.
template <typename t_CType> struct TCTemplateOwner {
  template <typename t_CResult = t_CType> t_CResult *operator->() const;
};
struct CTemplateOwningEntry {
  int m_Value;
  TCTemplateOwner<CItems> m_pItems;
};

// What runs implicitly: constructors with member and base initializers,
// destructors of locals, temporaries and members, also one that can't be
// seen, and an allocation function.
struct CInitClear {
  int m_Dummy;
  CInitClear(CItems &_Items) : m_Dummy((_Items.f_Clear(), 0)) {}
};
struct CBaseClear : CInitClear {
  CBaseClear(CItems &_Items) : CInitClear(_Items) {}
};
struct CClearOnExit {
  CItems &m_Items;
  ~CClearOnExit() { m_Items.f_Clear(); }
};
struct CHolderOfClear {
  CClearOnExit m_Clear;
};
struct COpaqueEnd {
  CItems &m_Items;
  ~COpaqueEnd();
};
struct CPlaced {
  static void *operator new(decltype(sizeof(0)) _Size, CItems &_Items) {
    _Items.f_Clear();
    return ::operator new(_Size);
  }
};
// Virtual members, also of a final class.
struct CProcessor {
  virtual ~CProcessor();
  virtual void f_Apply(CItems &_Items) {}
};
struct CFinalProcessor final : CProcessor {
  void f_Apply(CItems &_Items) override {}
};
// An entry whose removing member is virtual, which an override can run.
struct CVirtualRemoving {
  int m_Value;
  virtual void f_Clear() {}
};
// What a helper keeps a pointer to.
extern CItems *g_pSaved;
void fg_DeleteEntryRef(CEntry *&_pEntry);
// A collection whose subscript and `operator*` without bodies give itself.
struct CSelfIndexedDeclared {
  CEntry *f_FindEqual(int _Key);
  CSelfIndexedDeclared &operator[](int _Key);
  void f_Clear();
};
struct CSelfDerefDeclared {
  int *f_FindEqual(int _Key);
  void f_Clear();
  CSelfDerefDeclared &operator*();
};
struct CSelfDerefDeclaredEntry {
  int m_Value;
  CSelfDerefDeclared m_Children;
};

// Guards in each form, whose destructors change their own state, what they
// refer to, or what can't be seen.
struct CAggOwn {
  CItems &m_Items;
  CItems m_Own;
  ~CAggOwn() { m_Own.f_Clear(); }
};
struct CDerivedOwn : CAggOwn {};
struct CCtorOwn {
  CItems &m_Items;
  CItems m_Own;
  CCtorOwn(CItems &_Items) : m_Items(_Items) {}
  ~CCtorOwn() { m_Own.f_Clear(); }
};
struct CPtrOwn {
  CItems *m_pItems;
  CItems m_Own;
  CPtrOwn(CItems *const &_pItems) : m_pItems(_pItems) {}
  ~CPtrOwn() { m_Own.f_Clear(); }
};
struct CFactoryOwn {
  CAggOwn f_Make(CItems &_Items) { return {_Items}; }
  CAggOwn operator()(CItems &_Items, int _Unused) { return {_Items}; }
};
struct CItemsGuardOwn {
  CItems *m_pItems;
  CItems m_Own;
  ~CItemsGuardOwn() { m_Own.f_Clear(); }
};
struct CAggRef {
  CItems &m_Items;
  CItems m_Own;
  ~CAggRef() { m_Items.f_Clear(); }
};
struct CDerivedRef : CAggRef {};
struct CCtorRef {
  CItems &m_Items;
  CItems m_Own;
  CCtorRef(CItems &_Items) : m_Items(_Items) {}
  ~CCtorRef() { m_Items.f_Clear(); }
};
struct CPtrRef {
  CItems *m_pItems;
  CItems m_Own;
  CPtrRef(CItems *const &_pItems) : m_pItems(_pItems) {}
  ~CPtrRef() { m_pItems->f_Clear(); }
};
struct CFactoryRef {
  CAggRef f_Make(CItems &_Items) { return {_Items}; }
  CAggRef operator()(CItems &_Items, int _Unused) { return {_Items}; }
};
struct CItemsGuardRef {
  CItems *m_pItems;
  CItems m_Own;
  ~CItemsGuardRef() { m_pItems->f_Clear(); }
};
struct CAggUnknown {
  CItems &m_Items;
  CItems m_Own;
  ~CAggUnknown() ;
};
struct CDerivedUnknown : CAggUnknown {};
struct CCtorUnknown {
  CItems &m_Items;
  CItems m_Own;
  CCtorUnknown(CItems &_Items) : m_Items(_Items) {}
  ~CCtorUnknown() ;
};
struct CPtrUnknown {
  CItems *m_pItems;
  CItems m_Own;
  CPtrUnknown(CItems *const &_pItems) : m_pItems(_pItems) {}
  ~CPtrUnknown() ;
};
struct CFactoryUnknown {
  CAggUnknown f_Make(CItems &_Items) { return {_Items}; }
  CAggUnknown operator()(CItems &_Items, int _Unused) { return {_Items}; }
};
struct CItemsGuardUnknown {
  CItems *m_pItems;
  CItems m_Own;
  ~CItemsGuardUnknown() ;
};
// Guards whose kept pointer is the selected member of a union, after an
// unnamed bit-field, or in an array.
union CUnionGuard {
  int m_Unused;
  CItems *m_pItems;
  ~CUnionGuard() { m_pItems->f_Clear(); }
};
struct CBitGuard {
  int : 4;
  CItems *m_pItems;
  ~CBitGuard() { m_pItems->f_Clear(); }
};
struct CArrayGuard {
  CItems *m_pItems[1];
  ~CArrayGuard() { m_pItems[0]->f_Clear(); }
};
// Guards given what they keep after they are made, which clear it or their
// own state.
struct CLaterGuard {
  CItems *m_pItems = nullptr;
  ~CLaterGuard() {
    if (m_pItems)
      m_pItems->f_Clear();
  }
  void f_Set(CItems &_Items) { m_pItems = &_Items; }
};
struct CLaterOwnGuard {
  CItems *m_pItems = nullptr;
  CItems m_Own;
  ~CLaterOwnGuard() { m_Own.f_Clear(); }
};
void fg_FillGuard(CLaterGuard &_Guard, CItems &_Items);
void fg_ClearUnseen(CItems &_Items);
void fg_RedirectUnseen(CItems *&_pItems);
// Round 72: what a helper ends through a reference to a pointer to const,
// a guard given what it keeps only where that doesn't run, a guard whose
// wrapper member points to what it clears, and an entry whose pointer member
// points to what removes itself.
inline void fg_EndConst(CItems const *&_pItems) { _pItems->~CItems(); }
inline void fg_TidyConst(CItems &_Items) {
  CItems const *pItems = &_Items;
  fg_EndConst(pItems);
}
void fg_DisposeConst(CItems const *&_pItems);
inline void fg_ForwardConst(CItems const *&_pItems) { fg_DisposeConst(_pItems); }
inline void fg_DiscardedLater(CItems &_Items) {
  CLaterGuard Guard;
  if constexpr (false)
    Guard.m_pItems = &_Items;
}
inline void fg_UnevaluatedLater(CItems &_Items) {
  CLaterGuard Guard;
  (void)sizeof(Guard.m_pItems = &_Items);
}
inline void fg_NeverLater(CItems &_Items) {
  CLaterGuard Guard;
  if (false)
    Guard.m_pItems = &_Items;
}
template <typename t_CType> struct TCPointerWrapper {
  t_CType *m_pValue;
  t_CType *operator->() const { return m_pValue; }
};
struct CWrapperGuard {
  TCPointerWrapper<CItems> m_pItems;
  ~CWrapperGuard() { m_pItems->f_Clear(); }
};
inline void fg_WrapperGuard(CItems &_Items) {
  CWrapperGuard Guard{{&_Items}};
  (void)Guard;
}
struct CRemovable {
  int m_Value;
  void f_Remove();
};
struct CPointingEntry {
  int m_Value;
  CRemovable *m_pHeap;
};
// Round 73: a constructor inherited from one that clears what it is given,
// a guard whose destructor clears through a member function, an entry that
// removes itself through a member function or a helper, and what a local
// pointer to a collection in an entry clears.
struct CClearingBase {
  CClearingBase(CItems &_Items) { _Items.f_Clear(); }
};
struct CInheritsClearing : CClearingBase {
  using CClearingBase::CClearingBase;
};
struct CLookingBase {
  CLookingBase(CItems &_Items) { (void)_Items.f_FindEqual(1); }
};
struct CInheritsLooking : CLookingBase {
  using CLookingBase::CLookingBase;
};
inline void fg_InheritedClear(CItems &_Items) {
  CInheritsClearing Clearing(_Items);
  (void)Clearing;
}
inline void fg_InheritedLook(CItems &_Items) {
  CInheritsLooking Looking(_Items);
  (void)Looking;
}
struct CCleanupGuard {
  CItems *m_pItems;
  void f_Cleanup() { m_pItems->f_Clear(); }
  ~CCleanupGuard() { f_Cleanup(); }
};
struct COwnCleanupGuard {
  CItems *m_pItems;
  CItems m_Own;
  void f_Cleanup() { m_Own.f_Clear(); }
  ~COwnCleanupGuard() { f_Cleanup(); }
};
inline void fg_CleanupGuard(CItems &_Items) {
  CCleanupGuard Guard{&_Items};
  (void)Guard;
}
inline void fg_OwnCleanupGuard(CItems &_Items) {
  COwnCleanupGuard Guard{&_Items, {}};
  (void)Guard;
}
struct CTidyingEntry {
  int m_Value;
  void f_Remove();
  void f_Tidy() { f_Remove(); }
};
inline void fg_Erase(CTidyingEntry &_Entry) { _Entry.f_Remove(); }
inline void fg_ErasePointer(CTidyingEntry *_pEntry) { _pEntry->f_Remove(); }
inline void fg_ClearChildrenAlias(CItems &_Items) {
  auto *pChildren = &_Items[0].m_Children;
  pChildren->f_Clear();
}
inline void fg_ClearChildrenDirect(CItems &_Items) {
  _Items[0].m_Children.f_Clear();
}
// Round 74: an object made from a number, which owns its own buffer; a local
// array and a local object a constructor keeps a pointer in; a value that
// wraps a pointer to the collection; a collection in an entry cleared through
// a cast or as an element of an array; a collection in an entry whose
// member function clears the collection it is in through a pointer back.
struct CText {
  char *m_pBuffer;
  CText(int _Length) : m_pBuffer(new char[_Length]) {}
  ~CText() { delete[] m_pBuffer; }
};
inline int fg_Batch(CItems &_Items) {
  CEntry *Batch[1];
  Batch[0] = _Items.f_FindEqual(0);
  return Batch[0]->m_Value;
}
struct CObserver {
  CItems *m_pItems;
  CObserver(CItems &_Items) { m_pItems = &_Items; }
  ~CObserver() {}
};
inline void fg_Observe(CItems &_Items) {
  CObserver Observer(_Items);
  (void)Observer;
}
struct CItemsView {
  CItems *m_pItems;
  CItems &f_Get() const { return *m_pItems; }
};
inline CItemsView fg_View(CItems &_Items) { return {&_Items}; }
inline void fg_ClearView(CItems &_Items) { fg_View(_Items).f_Get().f_Clear(); }
struct CCastTidyEntry {
  int m_Value;
  CChildren m_Child;
  CChildren m_Children[2];
  void f_TidyCast() { static_cast<CChildren &>(m_Child).f_Clear(); }
  void f_TidySubscript() { m_Children[0].f_Clear(); }
  void f_TidyDirect() { m_Child.f_Clear(); }
};
struct CBackItems;
struct CBackChildren {
  CBackItems *m_pOwner;
  void f_Drop();
  void f_Clear();
};
struct CBackEntry {
  int m_Value;
  CBackChildren m_Children;
};
struct CBackItems {
  CBackEntry *f_FindEqual(int _Key);
  CBackEntry &operator[](int _Key);
  void f_Clear();
};
inline void CBackChildren::f_Drop() { m_pOwner->f_Clear(); }
inline void CBackChildren::f_Clear() { m_pOwner->f_Clear(); }
// A pointer back to what holds the collection the entry is in.
struct CBackOwnerEntry;
struct CBackOwnerItems {
  CBackOwnerEntry *f_FindEqual(int _Key);
  CBackOwnerEntry &operator[](int _Key);
  void f_Clear();
};
struct CBackOwner {
  CBackOwnerItems m_Items;
};
struct CBackOwnerChildren {
  CBackOwner *m_pOwner;
  void f_Clear();
};
struct CBackOwnerEntry {
  int m_Value;
  CBackOwnerChildren m_Children;
};
inline void CBackOwnerChildren::f_Clear() { m_pOwner->m_Items.f_Clear(); }
// Round 75: what a final class names through a `using` of a base's virtual
// function, whose override in between runs; a setter of a local pointer; a
// local stored to through a local alias.
struct CSelectNode {
  int m_Value;
};
struct CSelectBase : CSelectNode {
  virtual CSelectNode &f_Selected() { return *this; }
  virtual CSelectNode &operator[](int _Key) { return *this; }
};
struct CSelectMid : CSelectBase {
  TCEntries<CSelectNode> m_Nodes;
  CSelectNode &f_Selected() override { return m_Nodes[0]; }
  CSelectNode &operator[](int _Key) override { return m_Nodes[_Key]; }
};
struct CSelectItems final : CSelectMid {
  using CSelectBase::f_Selected;
};
struct CSelectRoot : CSelectMid {
  using CSelectBase::operator[];
};
struct CSelectOwn final : CSelectBase {};
inline void fg_SetItems(CItems *&o_pItems, CItems &_Items) {
  o_pItems = &_Items;
}
inline void fg_ClearThroughSetter(CItems &_Items) {
  CItems *pItems = nullptr;
  fg_SetItems(pItems, _Items);
  pItems->f_Clear();
}
inline void fg_LookThroughSetter(CItems &_Items) {
  CItems *pItems = nullptr;
  fg_SetItems(pItems, _Items);
  (void)pItems->f_FindEqual(1);
}
struct CPlainView {
  CItems *m_pItems;
};
inline void fg_StoreThroughAlias(CItems &_Items) {
  CPlainView View;
  auto &Alias = View;
  Alias.m_pItems = &_Items;
  (void)View;
}
inline void fg_StoreThroughPointerAlias(CItems &_Items) {
  CPlainView View;
  auto *pView = &View;
  pView->m_pItems = &_Items;
  (void)View;
}
// Round 76: what a local view, a reference wrapper or an array of pointers
// made of a collection clears; a guard given what it keeps through aliases
// and setters; a setter through an alias of the pointer, or where it never
// runs; a guard that restores plain data of an entry.
inline CClearingView::~CClearingView() {
  if (m_pItems)
    m_pItems->f_Clear();
}
template <typename t_CType> struct TCRef {
  t_CType *m_pValue;
  TCRef(t_CType &_Value) : m_pValue(&_Value) {}
  t_CType &f_Get() const { return *m_pValue; }
};
inline void fg_ClearLocalView(CItems &_Items) {
  auto View = fg_View(_Items);
  View.f_Get().f_Clear();
}
inline void fg_ClearLocalViewCopy(CItems &_Items) {
  auto View = fg_View(_Items);
  auto Copy = View;
  Copy.f_Get().f_Clear();
}
inline void fg_ClearThroughRef(CItems &_Items) {
  TCRef<CItems> Ref(_Items);
  Ref.f_Get().f_Clear();
}
inline void fg_ClearBatch(CItems &_Items) {
  CItems *Batch[] = {&_Items};
  Batch[0]->f_Clear();
}
inline void fg_ForwardClearBatch(CItems &_Items) { fg_ClearBatch(_Items); }
inline void fg_GuardThroughAlias(CItems &_Items) {
  CLaterGuard Guard;
  auto &Alias = Guard;
  Alias.m_pItems = &_Items;
}
inline void fg_GuardThroughPointer(CItems &_Items) {
  CLaterGuard Guard;
  auto *pGuard = &Guard;
  pGuard->m_pItems = &_Items;
}
inline void fg_GuardSetThroughPointer(CItems &_Items) {
  CLaterGuard Guard;
  auto *pGuard = &Guard;
  pGuard->f_Set(_Items);
}
inline void fg_GuardSetter(CItems &_Items) {
  CLaterGuard Guard;
  fg_SetItems(Guard.m_pItems, _Items);
}
inline void fg_ClearThroughSetterAlias(CItems &_Items) {
  CItems *pItems = nullptr;
  auto &rpItems = pItems;
  fg_SetItems(rpItems, _Items);
  pItems->f_Clear();
}
inline void fg_ClearNeverSet(CItems &_Items, CItems &_Other) {
  CItems *pItems = &_Other;
  if (false)
    fg_SetItems(pItems, _Items);
  pItems->f_Clear();
}
struct CRestore {
  int &m_rValue;
  int m_Saved;
  ~CRestore() { m_rValue = m_Saved; }
};
struct CCachedLink {
  CEntry *m_pCached;
};
// Round 77: a shared owner and what holds a collection and a pointer, whose
// copies keep what the source points to; a guard named to clear that only
// borrows; an assignment operator written for a part of an entry; a chain of
// helpers past the depth; a view given by value.
template <typename t_CType> struct TCSharedPointer {
  t_CType *m_pValue;
  t_CType *operator->() const { return m_pValue; }
  t_CType &operator*() const { return *m_pValue; }
  void f_Clear();
};
struct CSharedEntry {
  int m_Value;
  TCSharedPointer<CItems> m_pChildren;
};
struct CMixed {
  CItems *m_pItems;
  TCValues<int> m_Scratch;
};
inline void fg_ClearMixedCopy(CItems &_Items) {
  CMixed Original{&_Items, {}};
  CMixed Copy = Original;
  Copy.m_pItems->f_Clear();
}
struct CCancellable {
  CItems *m_pItems;
  ~CCancellable() {
    if (m_pItems)
      m_pItems->f_Clear();
  }
  void f_Clear() { m_pItems = nullptr; }
};
inline CCancellable fg_MakeCancellable(CItems &_Items) { return {&_Items}; }
inline void fg_UseCancellable(CItems &_Items) {
  auto Cleanup = fg_MakeCancellable(_Items);
  (void)Cleanup;
}
struct CRegistration {
  CItems *m_pItems = nullptr;
  CRegistration() = default;
  CRegistration &operator=(CRegistration &&_Other) {
    if (m_pItems)
      m_pItems->f_Remove(0);
    m_pItems = _Other.m_pItems;
    return *this;
  }
};
struct CRegisteredEntry {
  int m_Value;
  CRegistration m_Registration;
};
struct CChain {
  CItems *m_pItems;
  void f_Finalize() { m_pItems->f_Clear(); }
  void f_Apply() { f_Finalize(); }
  void f_Commit() { f_Apply(); }
};
inline void fg_ChainDeep(CItems &_Items) {
  CChain Chain{&_Items};
  Chain.f_Commit();
}
inline void fg_ChainMiddle(CItems &_Items) { fg_ChainDeep(_Items); }
inline void fg_ChainTidy(CItems &_Items) { fg_ChainMiddle(_Items); }
inline void fg_ClearViewCopy(CItemsView _View) { _View.f_Get().f_Clear(); }
inline void fg_TidyView(CItemsView &_View) { fg_ClearViewCopy(_View); }
// Round 78: a copy is new at its own level, what it points to the source's:
// a collection of pointers, an entry that holds a collection and a pointer,
// an optional that holds its value in it, an assignment operator with an
// explicit object parameter, and an entry with a reference member.
struct CMixedEntry {
  int m_Value;
  TCValues<int> m_Children;
  CItems *m_pOther;
};
template <typename t_CType> struct TCOptional {
  alignas(t_CType) unsigned char m_Storage[sizeof(t_CType)];
  bool m_bValid;
  t_CType *operator->();
  t_CType &operator*();
};
struct CExplicitRegistration {
  CItems *m_pItems = nullptr;
  CExplicitRegistration() = default;
  CExplicitRegistration &operator=(this CExplicitRegistration &_Self,
                                   CExplicitRegistration &&_Other) {
    if (_Self.m_pItems)
      _Self.m_pItems->f_Remove(0);
    _Self.m_pItems = _Other.m_pItems;
    return _Self;
  }
};
struct CExplicitEntry {
  int m_Value;
  CExplicitRegistration m_Registration;
};
struct CRefEntry {
  int m_Value;
  int &m_rValue;
};
inline void CScratchView::f_Clear() { m_pItems->f_Clear(); }
struct CPeerEntry {
  int m_Value;
  TCValues<int> m_Scratch;
  int *m_pPeer;
};
inline void fg_DisposeConst(CItems const *&_pItems) {
  delete _pItems;
  _pItems = nullptr;
}
// What a helper writes into what a parameter refers to.
struct CSlot {
  CItems *m_pItems;
};
extern CSlot g_Slot;
// A virtual call operator with an empty body, called on a member.
struct CVirtualCall {
  CEntry *f_FindEqual(int _Key);
  virtual void operator()() {}
};
// An entry whose removing member can't be seen.
struct COutOfLineRemoving {
  int m_Value;
  void f_Remove();
};

// A collection whose lookups and removals have bodies that can be seen, like
// a `TCMap`, whose lookup's returns can't be told: a node found in a loop.
template <typename t_CKey, typename t_CValue> struct TCVisibleMap {
  struct CNode {
    t_CKey m_Key;
    t_CValue m_Value;
    CNode *m_pNext;
  };
  CNode *m_pFirst = nullptr;
  t_CValue *f_FindEqual(t_CKey const &_Key) {
    for (CNode *pNode = m_pFirst; pNode; pNode = pNode->m_pNext)
      if (pNode->m_Key == _Key)
        return &pNode->m_Value;
    return nullptr;
  }
  t_CValue &operator[](t_CKey const &_Key) { return *f_FindEqual(_Key); }
  bool f_Remove(t_CKey const &_Key) {
    for (CNode **ppNode = &m_pFirst; *ppNode; ppNode = &(*ppNode)->m_pNext)
      if ((*ppNode)->m_Key == _Key) {
        CNode *pNode = *ppNode;
        *ppNode = pNode->m_pNext;
        delete pNode;
        return true;
      }
    return false;
  }
  void f_Clear() {
    while (m_pFirst)
      f_Remove(m_pFirst->m_Key);
  }
};
using CVisibleItems = TCVisibleMap<int, CEntry>;
// A map like `NContainer::TCMap`, whose lookup's body is seen but what it
// gives can't be told: a node found in a tree of links, its value in its
// storage.
struct CTreeLink {
  CTreeLink *m_pLeft = nullptr;
  CTreeLink *m_pRight = nullptr;
};
template <typename t_CNode> struct TCTree {
  CTreeLink *m_pRoot = nullptr;
  template <typename tf_CKey, typename tf_CCompare>
  t_CNode *f_FindEqual(tf_CKey const &_Key, tf_CCompare const &_Compare) {
    CTreeLink *pLink = m_pRoot;
    while (pLink) {
      t_CNode *pNode = static_cast<t_CNode *>(pLink);
      if (_Compare(pNode->f_Key(), _Key))
        pLink = pLink->m_pRight;
      else if (_Compare(_Key, pNode->f_Key()))
        pLink = pLink->m_pLeft;
      else
        return pNode;
    }
    return nullptr;
  }
  void f_Unlink(t_CNode *_pNode) {
    if (m_pRoot == _pNode)
      m_pRoot = _pNode->m_pRight;
  }
};
struct CTreeLess {
  bool operator()(int _Left, int _Right) const { return _Left < _Right; }
};
template <typename t_CKey, typename t_CValue> struct TCTreeMap {
  struct CNode : CTreeLink {
    alignas(t_CValue) unsigned char m_Storage[sizeof(t_CValue)];
    t_CKey m_Key;
    t_CKey const &f_Key() const { return m_Key; }
    t_CValue &f_Value() { return *reinterpret_cast<t_CValue *>(m_Storage); }
  };
  TCTree<CNode> mp_Tree;
  CTreeLess mp_Compare;
  template <typename tf_CKey> t_CValue *f_FindEqual(tf_CKey &&_Key) {
    CNode *pData = mp_Tree.f_FindEqual(_Key, mp_Compare);
    if (pData)
      return &pData->f_Value();
    else
      return nullptr;
  }
  t_CValue &operator[](t_CKey const &_Key) { return *f_FindEqual(_Key); }
  bool f_Remove(t_CKey const &_Key) {
    CNode *pNode = mp_Tree.f_FindEqual(_Key, mp_Compare);
    if (!pNode)
      return false;
    mp_Tree.f_Unlink(pNode);
    delete pNode;
    return true;
  }
  void f_Clear() {
    while (mp_Tree.m_pRoot)
      f_Remove(static_cast<CNode *>(mp_Tree.m_pRoot)->m_Key);
  }
};
using CTreeItems = TCTreeMap<int, CEntry>;
void fg_TouchEntryOpaque(CEntry const &_Entry);

// What can change an entry, given it not const, and what can't.
void fg_ChangeEntry(CEntry &_Entry);
void fg_Inspect(const CEntry &_Entry);
// What gives back what it is given, without a body, and through a local
// alias.
int &fg_PassSlot(const int &_Slot);
inline const int &fg_AliasConst(const int &_Value) {
  const int &Alias = _Value;
  return Alias;
}
inline void CUnionEntry::f_WriteThroughAlias() {
  const_cast<int &>(fg_AliasConst(m_Empty)) = 0;
}

struct CApp : CActor {
  CItems mp_Items;
  CDerivedEntries mp_Derived;
  CValueItems mp_Values;
  std::array<int, 2> *m_pValues = nullptr;
  CUnionEntries mp_UnionEntries;
  CConstCall mp_ConstCall;
  std::array<CWrapped, 2> mp_Wrappers;
  CItems mp_Other;
  CWrapped mp_Wrapped;
  CItems mp_Array[2];
  std::array<CItems, 2> mp_StdArray;
  CVirtualItems mp_Virtual;
  CUnionItems mp_Union;
  TCOwner<CItems> mp_pOwned;
  COwningEntries mp_OwningEntries;
  TCEntries<CNode> mp_Nodes;
  CVisibleItems mp_Visible;
  CTreeItems mp_Tree;
  TCEntries<CPointingEntry> mp_Pointing;
  TCEntries<CTidyingEntry> mp_Tidying;
  TCEntries<CCastTidyEntry> mp_CastTidy;
  CBackItems mp_BackItems;
  CBackOwner mp_BackOwner;
  CSelectItems mp_Selecting;
  CSelectRoot mp_SelectRoot;
  CSelectOwn mp_SelectOwn;
  TCEntries<CCachedLink> mp_Links;
  TCEntries<CPeerEntry> mp_Peers;
  TCEntries<CSharedEntry> mp_SharedEntries;
  TCEntries<CRegisteredEntry> mp_Registered;
  CItemsView mp_View;
  TCValues<CEntry *> mp_EntryPointers;
  TCEntries<CMixedEntry> mp_MixedEntries;
  TCOptional<CItems> mp_OptionalItems;
  TCEntries<CExplicitEntry> mp_Explicit;
  TCEntries<CRefEntry> mp_RefEntries;
  static void fg_ClearOther(CApp *_pThis) { _pThis->mp_Other.f_Clear(); }
  static void fg_ClearOwn(CApp *_pThis) { _pThis->mp_Items.f_Clear(); }
  TCValues<int> mp_Counts;
  CItems &f_OtherItems() { return mp_Other; }
  CItems const *mp_pConstItems = nullptr;
  CVirtualCall mp_VirtualCall;
  TCEntries<COutOfLineRemoving> mp_OutOfLine;
  CProcessor &m_Processor = *(CProcessor *)nullptr;
  CProcessor mp_ProcessorValue;
  TCEntries<CVirtualRemoving> mp_VirtualRemoving;
  CSelfIndexedDeclared mp_SelfIndexedDeclared;
  TCEntries<CSelfDerefDeclaredEntry> mp_SelfDerefDeclared;
  TCEntries<CSelfDerefEntry> mp_SelfDerefs;
  TCEntries<CTemplateOwningEntry> mp_TemplateOwning;
  TCEntries<TCOwner<CHeapData>> mp_Owners;
  TCEntries<TCClearableOwner<CHeapData>> mp_ClearableOwners;
  TCEntries<CHolderEntry> mp_Holders;
  TCEntries<CStateEntry> mp_States;
  CIteratorItems mp_IteratorItems;
  TCEntries<CRingA> mp_Rings;
  TCEntries<CInheritedOwningEntry> mp_Inherited;
  TCEntries<CStarOwningEntry> mp_Star;
  TCEntries<CHeapEntry> mp_Heap;
  TCEntries<CSelfRemoving> mp_SelfRemoving;
  TCEntries<CVirtualEntry> mp_VirtualEntries;
  CSelfIndexed mp_SelfIndexed;
  std::array<int, 2> mp_Fixed;
  bool m_bChoose = false;

  // In the callback: each form of the collection, removed from, moved or
  // deleted from, reported.
  TCFuture<int> f_RemoveDirect() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      mp_Items.f_Remove(0);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RemoveCast() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      static_cast<CItems &>(mp_Items).f_Remove(0);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RemoveConditional() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      (m_bChoose ? mp_Items : mp_Items).f_Remove(0);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RemoveComma() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      ((void)0, mp_Items).f_Remove(0);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RemoveReference() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      auto &Items = mp_Items;
      Items.f_Remove(0);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RemovePointer() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      auto *pItems = &mp_Items;
      (*pItems).f_Remove(0);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RemoveThisPointer() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      auto *pThis = this;
      pThis->mp_Items.f_Remove(0);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RemoveThisDeref() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      (*this).mp_Items.f_Remove(0);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RemoveSubobject() {
    CEntry *pEntry = mp_Wrapped.m_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Wrapped.m_Items.f_FindEqual(0);
      mp_Wrapped.m_Items.f_Remove(0);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_MoveDirect() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      CItems Taken = fg_Move(mp_Items);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_MoveCast() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      CItems Taken = fg_Move(static_cast<CItems &>(mp_Items));
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_MoveConditional() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      CItems Taken = fg_Move((m_bChoose ? mp_Items : mp_Items));
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_MoveComma() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      CItems Taken = fg_Move(((void)0, mp_Items));
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_MoveReference() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      auto &Items = mp_Items;
      CItems Taken = fg_Move(Items);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_MovePointer() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      auto *pItems = &mp_Items;
      CItems Taken = fg_Move((*pItems));
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_MoveThisPointer() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      auto *pThis = this;
      CItems Taken = fg_Move(pThis->mp_Items);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_MoveThisDeref() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      CItems Taken = fg_Move((*this).mp_Items);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_MoveSubobject() {
    CEntry *pEntry = mp_Wrapped.m_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Wrapped.m_Items.f_FindEqual(0);
      CItems Taken = fg_Move(mp_Wrapped.m_Items);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_DeleteDirect() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      delete mp_Items[0].m_pNext;
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_DeleteCast() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      delete static_cast<CItems &>(mp_Items)[0].m_pNext;
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_DeleteConditional() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      delete (m_bChoose ? mp_Items : mp_Items)[0].m_pNext;
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_DeleteComma() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      delete ((void)0, mp_Items)[0].m_pNext;
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_DeleteReference() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      auto &Items = mp_Items;
      delete Items[0].m_pNext;
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_DeletePointer() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      auto *pItems = &mp_Items;
      delete (*pItems)[0].m_pNext;
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_DeleteThisPointer() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      auto *pThis = this;
      delete pThis->mp_Items[0].m_pNext;
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_DeleteThisDeref() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      delete (*this).mp_Items[0].m_pNext;
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_DeleteSubobject() {
    CEntry *pEntry = mp_Wrapped.m_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Wrapped.m_Items.f_FindEqual(0);
      delete mp_Wrapped.m_Items[0].m_pNext;
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }

  // In the callback: each form of an entry deleted from, reported; removed
  // from by name, replaced or moved, which changes the collection it holds,
  // reported for a lookup in that collection, not for the entry itself;
  // updated, neither.
  TCFuture<int> f_EntryRemoveChildrenSubscript() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      mp_Items[0].m_Children.f_Remove(0);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_EntryChildRemoveChildrenSubscript() {
    int *pEntry = mp_Items[0].m_Children.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items[0].m_Children.f_FindEqual(0);
      mp_Items[0].m_Children.f_Remove(0);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_EntryRemoveChildrenReference() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      mp_Items.f_Get(0).m_Children.f_Remove(0);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_EntryChildRemoveChildrenReference() {
    int *pEntry = mp_Items[0].m_Children.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items[0].m_Children.f_FindEqual(0);
      mp_Items.f_Get(0).m_Children.f_Remove(0);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_EntryRemoveChildrenPointer() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      (*mp_Items.f_FindEqual(0)).m_Children.f_Remove(0);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_EntryChildRemoveChildrenPointer() {
    int *pEntry = mp_Items[0].m_Children.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items[0].m_Children.f_FindEqual(0);
      (*mp_Items.f_FindEqual(0)).m_Children.f_Remove(0);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_EntryRemoveChildrenAlias() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      auto &Entry = mp_Items[0];
      Entry.m_Children.f_Remove(0);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_EntryChildRemoveChildrenAlias() {
    int *pEntry = mp_Items[0].m_Children.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items[0].m_Children.f_FindEqual(0);
      auto &Entry = mp_Items[0];
      Entry.m_Children.f_Remove(0);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_EntryRemoveChildrenPointerAlias() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      auto *pAt = &mp_Items[0];
      (*pAt).m_Children.f_Remove(0);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_EntryChildRemoveChildrenPointerAlias() {
    int *pEntry = mp_Items[0].m_Children.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items[0].m_Children.f_FindEqual(0);
      auto *pAt = &mp_Items[0];
      (*pAt).m_Children.f_Remove(0);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_EntryRemoveChildrenIterator() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      auto iEntry = mp_Items.f_GetIterator();
      (*iEntry).m_Children.f_Remove(0);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_EntryChildRemoveChildrenIterator() {
    int *pEntry = mp_Items[0].m_Children.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items[0].m_Children.f_FindEqual(0);
      auto iEntry = mp_Items.f_GetIterator();
      (*iEntry).m_Children.f_Remove(0);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_EntryTidyChildrenSubscript() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      mp_Items[0].m_Children.f_Tidy();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_EntryChildTidyChildrenSubscript() {
    int *pEntry = mp_Items[0].m_Children.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items[0].m_Children.f_FindEqual(0);
      mp_Items[0].m_Children.f_Tidy();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_EntryTidyChildrenReference() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      mp_Items.f_Get(0).m_Children.f_Tidy();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_EntryChildTidyChildrenReference() {
    int *pEntry = mp_Items[0].m_Children.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items[0].m_Children.f_FindEqual(0);
      mp_Items.f_Get(0).m_Children.f_Tidy();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_EntryTidyChildrenPointer() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      (*mp_Items.f_FindEqual(0)).m_Children.f_Tidy();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_EntryChildTidyChildrenPointer() {
    int *pEntry = mp_Items[0].m_Children.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items[0].m_Children.f_FindEqual(0);
      (*mp_Items.f_FindEqual(0)).m_Children.f_Tidy();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_EntryTidyChildrenAlias() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      auto &Entry = mp_Items[0];
      Entry.m_Children.f_Tidy();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_EntryChildTidyChildrenAlias() {
    int *pEntry = mp_Items[0].m_Children.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items[0].m_Children.f_FindEqual(0);
      auto &Entry = mp_Items[0];
      Entry.m_Children.f_Tidy();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_EntryTidyChildrenPointerAlias() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      auto *pAt = &mp_Items[0];
      (*pAt).m_Children.f_Tidy();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_EntryChildTidyChildrenPointerAlias() {
    int *pEntry = mp_Items[0].m_Children.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items[0].m_Children.f_FindEqual(0);
      auto *pAt = &mp_Items[0];
      (*pAt).m_Children.f_Tidy();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_EntryTidyChildrenIterator() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      auto iEntry = mp_Items.f_GetIterator();
      (*iEntry).m_Children.f_Tidy();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_EntryChildTidyChildrenIterator() {
    int *pEntry = mp_Items[0].m_Children.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items[0].m_Children.f_FindEqual(0);
      auto iEntry = mp_Items.f_GetIterator();
      (*iEntry).m_Children.f_Tidy();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_EntryResetChildrenSubscript() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      mp_Items[0].f_ResetChildren();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_EntryChildResetChildrenSubscript() {
    int *pEntry = mp_Items[0].m_Children.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items[0].m_Children.f_FindEqual(0);
      mp_Items[0].f_ResetChildren();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_EntryResetChildrenReference() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      mp_Items.f_Get(0).f_ResetChildren();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_EntryChildResetChildrenReference() {
    int *pEntry = mp_Items[0].m_Children.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items[0].m_Children.f_FindEqual(0);
      mp_Items.f_Get(0).f_ResetChildren();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_EntryResetChildrenPointer() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      (*mp_Items.f_FindEqual(0)).f_ResetChildren();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_EntryChildResetChildrenPointer() {
    int *pEntry = mp_Items[0].m_Children.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items[0].m_Children.f_FindEqual(0);
      (*mp_Items.f_FindEqual(0)).f_ResetChildren();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_EntryResetChildrenAlias() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      auto &Entry = mp_Items[0];
      Entry.f_ResetChildren();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_EntryChildResetChildrenAlias() {
    int *pEntry = mp_Items[0].m_Children.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items[0].m_Children.f_FindEqual(0);
      auto &Entry = mp_Items[0];
      Entry.f_ResetChildren();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_EntryResetChildrenPointerAlias() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      auto *pAt = &mp_Items[0];
      (*pAt).f_ResetChildren();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_EntryChildResetChildrenPointerAlias() {
    int *pEntry = mp_Items[0].m_Children.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items[0].m_Children.f_FindEqual(0);
      auto *pAt = &mp_Items[0];
      (*pAt).f_ResetChildren();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_EntryResetChildrenIterator() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      auto iEntry = mp_Items.f_GetIterator();
      (*iEntry).f_ResetChildren();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_EntryChildResetChildrenIterator() {
    int *pEntry = mp_Items[0].m_Children.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items[0].m_Children.f_FindEqual(0);
      auto iEntry = mp_Items.f_GetIterator();
      (*iEntry).f_ResetChildren();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_EntryDeleteSubscript() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      delete mp_Items[0].m_pNext;
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_EntryChildDeleteSubscript() {
    int *pEntry = mp_Items[0].m_Children.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items[0].m_Children.f_FindEqual(0);
      delete mp_Items[0].m_pNext;
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_EntryDeleteReference() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      delete mp_Items.f_Get(0).m_pNext;
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_EntryChildDeleteReference() {
    int *pEntry = mp_Items[0].m_Children.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items[0].m_Children.f_FindEqual(0);
      delete mp_Items.f_Get(0).m_pNext;
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_EntryDeletePointer() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      delete (*mp_Items.f_FindEqual(0)).m_pNext;
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_EntryChildDeletePointer() {
    int *pEntry = mp_Items[0].m_Children.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items[0].m_Children.f_FindEqual(0);
      delete (*mp_Items.f_FindEqual(0)).m_pNext;
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_EntryDeleteAlias() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      auto &Entry = mp_Items[0];
      delete Entry.m_pNext;
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_EntryChildDeleteAlias() {
    int *pEntry = mp_Items[0].m_Children.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items[0].m_Children.f_FindEqual(0);
      auto &Entry = mp_Items[0];
      delete Entry.m_pNext;
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_EntryDeletePointerAlias() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      auto *pAt = &mp_Items[0];
      delete (*pAt).m_pNext;
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_EntryChildDeletePointerAlias() {
    int *pEntry = mp_Items[0].m_Children.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items[0].m_Children.f_FindEqual(0);
      auto *pAt = &mp_Items[0];
      delete (*pAt).m_pNext;
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_EntryDeleteIterator() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      auto iEntry = mp_Items.f_GetIterator();
      delete (*iEntry).m_pNext;
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_EntryChildDeleteIterator() {
    int *pEntry = mp_Items[0].m_Children.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items[0].m_Children.f_FindEqual(0);
      auto iEntry = mp_Items.f_GetIterator();
      delete (*iEntry).m_pNext;
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_EntryMoveSubscript() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      CEntry Taken = fg_Move(mp_Items[0]);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_EntryChildMoveSubscript() {
    int *pEntry = mp_Items[0].m_Children.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items[0].m_Children.f_FindEqual(0);
      CEntry Taken = fg_Move(mp_Items[0]);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_EntryMoveReference() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      CEntry Taken = fg_Move(mp_Items.f_Get(0));
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_EntryChildMoveReference() {
    int *pEntry = mp_Items[0].m_Children.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items[0].m_Children.f_FindEqual(0);
      CEntry Taken = fg_Move(mp_Items.f_Get(0));
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_EntryMovePointer() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      CEntry Taken = fg_Move((*mp_Items.f_FindEqual(0)));
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_EntryChildMovePointer() {
    int *pEntry = mp_Items[0].m_Children.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items[0].m_Children.f_FindEqual(0);
      CEntry Taken = fg_Move((*mp_Items.f_FindEqual(0)));
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_EntryMoveAlias() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      auto &Entry = mp_Items[0];
      CEntry Taken = fg_Move(Entry);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_EntryChildMoveAlias() {
    int *pEntry = mp_Items[0].m_Children.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items[0].m_Children.f_FindEqual(0);
      auto &Entry = mp_Items[0];
      CEntry Taken = fg_Move(Entry);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_EntryMovePointerAlias() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      auto *pAt = &mp_Items[0];
      CEntry Taken = fg_Move((*pAt));
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_EntryChildMovePointerAlias() {
    int *pEntry = mp_Items[0].m_Children.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items[0].m_Children.f_FindEqual(0);
      auto *pAt = &mp_Items[0];
      CEntry Taken = fg_Move((*pAt));
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_EntryMoveIterator() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      auto iEntry = mp_Items.f_GetIterator();
      CEntry Taken = fg_Move((*iEntry));
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_EntryChildMoveIterator() {
    int *pEntry = mp_Items[0].m_Children.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items[0].m_Children.f_FindEqual(0);
      auto iEntry = mp_Items.f_GetIterator();
      CEntry Taken = fg_Move((*iEntry));
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_EntryUpdateSubscript() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      mp_Items[0].f_Update();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_EntryChildUpdateSubscript() {
    int *pEntry = mp_Items[0].m_Children.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items[0].m_Children.f_FindEqual(0);
      mp_Items[0].f_Update();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
  }
  TCFuture<int> f_EntryUpdateReference() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      mp_Items.f_Get(0).f_Update();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_EntryChildUpdateReference() {
    int *pEntry = mp_Items[0].m_Children.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items[0].m_Children.f_FindEqual(0);
      mp_Items.f_Get(0).f_Update();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
  }
  TCFuture<int> f_EntryUpdatePointer() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      (*mp_Items.f_FindEqual(0)).f_Update();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_EntryChildUpdatePointer() {
    int *pEntry = mp_Items[0].m_Children.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items[0].m_Children.f_FindEqual(0);
      (*mp_Items.f_FindEqual(0)).f_Update();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
  }
  TCFuture<int> f_EntryUpdateAlias() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      auto &Entry = mp_Items[0];
      Entry.f_Update();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_EntryChildUpdateAlias() {
    int *pEntry = mp_Items[0].m_Children.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items[0].m_Children.f_FindEqual(0);
      auto &Entry = mp_Items[0];
      Entry.f_Update();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
  }
  TCFuture<int> f_EntryUpdatePointerAlias() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      auto *pAt = &mp_Items[0];
      (*pAt).f_Update();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_EntryChildUpdatePointerAlias() {
    int *pEntry = mp_Items[0].m_Children.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items[0].m_Children.f_FindEqual(0);
      auto *pAt = &mp_Items[0];
      (*pAt).f_Update();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
  }
  TCFuture<int> f_EntryUpdateIterator() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      auto iEntry = mp_Items.f_GetIterator();
      (*iEntry).f_Update();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_EntryChildUpdateIterator() {
    int *pEntry = mp_Items[0].m_Children.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items[0].m_Children.f_FindEqual(0);
      auto iEntry = mp_Items.f_GetIterator();
      (*iEntry).f_Update();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
  }
  TCFuture<int> f_EntryReplaceSubscript() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      mp_Items[0].f_Renew();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_EntryChildReplaceSubscript() {
    int *pEntry = mp_Items[0].m_Children.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items[0].m_Children.f_FindEqual(0);
      mp_Items[0].f_Renew();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_EntryReplaceReference() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      mp_Items.f_Get(0).f_Renew();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_EntryChildReplaceReference() {
    int *pEntry = mp_Items[0].m_Children.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items[0].m_Children.f_FindEqual(0);
      mp_Items.f_Get(0).f_Renew();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_EntryReplacePointer() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      (*mp_Items.f_FindEqual(0)).f_Renew();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_EntryChildReplacePointer() {
    int *pEntry = mp_Items[0].m_Children.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items[0].m_Children.f_FindEqual(0);
      (*mp_Items.f_FindEqual(0)).f_Renew();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_EntryReplaceAlias() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      auto &Entry = mp_Items[0];
      Entry.f_Renew();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_EntryChildReplaceAlias() {
    int *pEntry = mp_Items[0].m_Children.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items[0].m_Children.f_FindEqual(0);
      auto &Entry = mp_Items[0];
      Entry.f_Renew();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_EntryReplacePointerAlias() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      auto *pAt = &mp_Items[0];
      (*pAt).f_Renew();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_EntryChildReplacePointerAlias() {
    int *pEntry = mp_Items[0].m_Children.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items[0].m_Children.f_FindEqual(0);
      auto *pAt = &mp_Items[0];
      (*pAt).f_Renew();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_EntryReplaceIterator() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      auto iEntry = mp_Items.f_GetIterator();
      (*iEntry).f_Renew();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_EntryChildReplaceIterator() {
    int *pEntry = mp_Items[0].m_Children.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items[0].m_Children.f_FindEqual(0);
      auto iEntry = mp_Items.f_GetIterator();
      (*iEntry).f_Renew();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }

  // In the callback: an iterator removing its entry, reported; stepping, which
  // assigns the iterator, not.
  TCFuture<int> f_IteratorRemove() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      auto iEntry = mp_Items.f_GetIterator();
      iEntry.f_Remove();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_IteratorNext() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      auto iEntry = mp_Items.f_GetIterator();
      iEntry.f_Next();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }

  // In the helper: each form of the collection, removed from, moved or
  // deleted from, reported.
  static void f_HelperRemoveDirectBody(CItems &_Items, CWrapped &_Wrapped, bool _bChoose) {
    _Items.f_Remove(0);
  }
  TCFuture<int> f_HelperRemoveDirect() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      f_HelperRemoveDirectBody(mp_Items, mp_Wrapped, m_bChoose);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  static void f_HelperRemoveCastBody(CItems &_Items, CWrapped &_Wrapped, bool _bChoose) {
    static_cast<CItems &>(_Items).f_Remove(0);
  }
  TCFuture<int> f_HelperRemoveCast() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      f_HelperRemoveCastBody(mp_Items, mp_Wrapped, m_bChoose);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  static void f_HelperRemoveConditionalBody(CItems &_Items, CWrapped &_Wrapped, bool _bChoose) {
    (_bChoose ? _Items : _Items).f_Remove(0);
  }
  TCFuture<int> f_HelperRemoveConditional() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      f_HelperRemoveConditionalBody(mp_Items, mp_Wrapped, m_bChoose);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  static void f_HelperRemoveCommaBody(CItems &_Items, CWrapped &_Wrapped, bool _bChoose) {
    ((void)0, _Items).f_Remove(0);
  }
  TCFuture<int> f_HelperRemoveComma() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      f_HelperRemoveCommaBody(mp_Items, mp_Wrapped, m_bChoose);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  static void f_HelperRemoveReferenceBody(CItems &_Items, CWrapped &_Wrapped, bool _bChoose) {
    auto &Items = _Items;
    Items.f_Remove(0);
  }
  TCFuture<int> f_HelperRemoveReference() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      f_HelperRemoveReferenceBody(mp_Items, mp_Wrapped, m_bChoose);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  static void f_HelperRemovePointerBody(CItems &_Items, CWrapped &_Wrapped, bool _bChoose) {
    auto *pItems = &_Items;
    (*pItems).f_Remove(0);
  }
  TCFuture<int> f_HelperRemovePointer() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      f_HelperRemovePointerBody(mp_Items, mp_Wrapped, m_bChoose);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  static void f_HelperRemoveSubobjectBody(CItems &_Items, CWrapped &_Wrapped, bool _bChoose) {
    _Wrapped.m_Items.f_Remove(0);
  }
  TCFuture<int> f_HelperRemoveSubobject() {
    CEntry *pEntry = mp_Wrapped.m_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Wrapped.m_Items.f_FindEqual(0);
      f_HelperRemoveSubobjectBody(mp_Items, mp_Wrapped, m_bChoose);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  static void f_HelperMoveDirectBody(CItems &_Items, CWrapped &_Wrapped, bool _bChoose) {
    CItems Taken = fg_Move(_Items);
  }
  TCFuture<int> f_HelperMoveDirect() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      f_HelperMoveDirectBody(mp_Items, mp_Wrapped, m_bChoose);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  static void f_HelperMoveCastBody(CItems &_Items, CWrapped &_Wrapped, bool _bChoose) {
    CItems Taken = fg_Move(static_cast<CItems &>(_Items));
  }
  TCFuture<int> f_HelperMoveCast() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      f_HelperMoveCastBody(mp_Items, mp_Wrapped, m_bChoose);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  static void f_HelperMoveConditionalBody(CItems &_Items, CWrapped &_Wrapped, bool _bChoose) {
    CItems Taken = fg_Move((_bChoose ? _Items : _Items));
  }
  TCFuture<int> f_HelperMoveConditional() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      f_HelperMoveConditionalBody(mp_Items, mp_Wrapped, m_bChoose);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  static void f_HelperMoveCommaBody(CItems &_Items, CWrapped &_Wrapped, bool _bChoose) {
    CItems Taken = fg_Move(((void)0, _Items));
  }
  TCFuture<int> f_HelperMoveComma() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      f_HelperMoveCommaBody(mp_Items, mp_Wrapped, m_bChoose);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  static void f_HelperMoveReferenceBody(CItems &_Items, CWrapped &_Wrapped, bool _bChoose) {
    auto &Items = _Items;
    CItems Taken = fg_Move(Items);
  }
  TCFuture<int> f_HelperMoveReference() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      f_HelperMoveReferenceBody(mp_Items, mp_Wrapped, m_bChoose);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  static void f_HelperMovePointerBody(CItems &_Items, CWrapped &_Wrapped, bool _bChoose) {
    auto *pItems = &_Items;
    CItems Taken = fg_Move((*pItems));
  }
  TCFuture<int> f_HelperMovePointer() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      f_HelperMovePointerBody(mp_Items, mp_Wrapped, m_bChoose);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  static void f_HelperMoveSubobjectBody(CItems &_Items, CWrapped &_Wrapped, bool _bChoose) {
    CItems Taken = fg_Move(_Wrapped.m_Items);
  }
  TCFuture<int> f_HelperMoveSubobject() {
    CEntry *pEntry = mp_Wrapped.m_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Wrapped.m_Items.f_FindEqual(0);
      f_HelperMoveSubobjectBody(mp_Items, mp_Wrapped, m_bChoose);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  static void f_HelperDeleteDirectBody(CItems &_Items, CWrapped &_Wrapped, bool _bChoose) {
    delete _Items[0].m_pNext;
  }
  TCFuture<int> f_HelperDeleteDirect() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      f_HelperDeleteDirectBody(mp_Items, mp_Wrapped, m_bChoose);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  static void f_HelperDeleteCastBody(CItems &_Items, CWrapped &_Wrapped, bool _bChoose) {
    delete static_cast<CItems &>(_Items)[0].m_pNext;
  }
  TCFuture<int> f_HelperDeleteCast() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      f_HelperDeleteCastBody(mp_Items, mp_Wrapped, m_bChoose);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  static void f_HelperDeleteConditionalBody(CItems &_Items, CWrapped &_Wrapped, bool _bChoose) {
    delete (_bChoose ? _Items : _Items)[0].m_pNext;
  }
  TCFuture<int> f_HelperDeleteConditional() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      f_HelperDeleteConditionalBody(mp_Items, mp_Wrapped, m_bChoose);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  static void f_HelperDeleteCommaBody(CItems &_Items, CWrapped &_Wrapped, bool _bChoose) {
    delete ((void)0, _Items)[0].m_pNext;
  }
  TCFuture<int> f_HelperDeleteComma() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      f_HelperDeleteCommaBody(mp_Items, mp_Wrapped, m_bChoose);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  static void f_HelperDeleteReferenceBody(CItems &_Items, CWrapped &_Wrapped, bool _bChoose) {
    auto &Items = _Items;
    delete Items[0].m_pNext;
  }
  TCFuture<int> f_HelperDeleteReference() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      f_HelperDeleteReferenceBody(mp_Items, mp_Wrapped, m_bChoose);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  static void f_HelperDeletePointerBody(CItems &_Items, CWrapped &_Wrapped, bool _bChoose) {
    auto *pItems = &_Items;
    delete (*pItems)[0].m_pNext;
  }
  TCFuture<int> f_HelperDeletePointer() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      f_HelperDeletePointerBody(mp_Items, mp_Wrapped, m_bChoose);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  static void f_HelperDeleteSubobjectBody(CItems &_Items, CWrapped &_Wrapped, bool _bChoose) {
    delete _Wrapped.m_Items[0].m_pNext;
  }
  TCFuture<int> f_HelperDeleteSubobject() {
    CEntry *pEntry = mp_Wrapped.m_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Wrapped.m_Items.f_FindEqual(0);
      f_HelperDeleteSubobjectBody(mp_Items, mp_Wrapped, m_bChoose);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }

  // In the helper: each form of an entry deleted from, reported; removed
  // from by name, replaced or moved, which changes the collection it holds,
  // reported for a lookup in that collection, not for the entry itself;
  // updated, neither.
  static void f_HelperEntryRemoveChildrenSubscriptBody(CItems &_Items, CWrapped &_Wrapped, bool _bChoose) {
    _Items[0].m_Children.f_Remove(0);
  }
  TCFuture<int> f_HelperEntryRemoveChildrenSubscript() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      f_HelperEntryRemoveChildrenSubscriptBody(mp_Items, mp_Wrapped, m_bChoose);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  static void f_HelperEntryChildRemoveChildrenSubscriptBody(CItems &_Items, CWrapped &_Wrapped, bool _bChoose) {
    _Items[0].m_Children.f_Remove(0);
  }
  TCFuture<int> f_HelperEntryChildRemoveChildrenSubscript() {
    int *pEntry = mp_Items[0].m_Children.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items[0].m_Children.f_FindEqual(0);
      f_HelperEntryChildRemoveChildrenSubscriptBody(mp_Items, mp_Wrapped, m_bChoose);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  static void f_HelperEntryRemoveChildrenReferenceBody(CItems &_Items, CWrapped &_Wrapped, bool _bChoose) {
    _Items.f_Get(0).m_Children.f_Remove(0);
  }
  TCFuture<int> f_HelperEntryRemoveChildrenReference() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      f_HelperEntryRemoveChildrenReferenceBody(mp_Items, mp_Wrapped, m_bChoose);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  static void f_HelperEntryChildRemoveChildrenReferenceBody(CItems &_Items, CWrapped &_Wrapped, bool _bChoose) {
    _Items.f_Get(0).m_Children.f_Remove(0);
  }
  TCFuture<int> f_HelperEntryChildRemoveChildrenReference() {
    int *pEntry = mp_Items[0].m_Children.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items[0].m_Children.f_FindEqual(0);
      f_HelperEntryChildRemoveChildrenReferenceBody(mp_Items, mp_Wrapped, m_bChoose);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  static void f_HelperEntryRemoveChildrenPointerBody(CItems &_Items, CWrapped &_Wrapped, bool _bChoose) {
    (*_Items.f_FindEqual(0)).m_Children.f_Remove(0);
  }
  TCFuture<int> f_HelperEntryRemoveChildrenPointer() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      f_HelperEntryRemoveChildrenPointerBody(mp_Items, mp_Wrapped, m_bChoose);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  static void f_HelperEntryChildRemoveChildrenPointerBody(CItems &_Items, CWrapped &_Wrapped, bool _bChoose) {
    (*_Items.f_FindEqual(0)).m_Children.f_Remove(0);
  }
  TCFuture<int> f_HelperEntryChildRemoveChildrenPointer() {
    int *pEntry = mp_Items[0].m_Children.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items[0].m_Children.f_FindEqual(0);
      f_HelperEntryChildRemoveChildrenPointerBody(mp_Items, mp_Wrapped, m_bChoose);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  static void f_HelperEntryRemoveChildrenAliasBody(CItems &_Items, CWrapped &_Wrapped, bool _bChoose) {
    auto &Entry = _Items[0];
    Entry.m_Children.f_Remove(0);
  }
  TCFuture<int> f_HelperEntryRemoveChildrenAlias() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      f_HelperEntryRemoveChildrenAliasBody(mp_Items, mp_Wrapped, m_bChoose);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  static void f_HelperEntryChildRemoveChildrenAliasBody(CItems &_Items, CWrapped &_Wrapped, bool _bChoose) {
    auto &Entry = _Items[0];
    Entry.m_Children.f_Remove(0);
  }
  TCFuture<int> f_HelperEntryChildRemoveChildrenAlias() {
    int *pEntry = mp_Items[0].m_Children.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items[0].m_Children.f_FindEqual(0);
      f_HelperEntryChildRemoveChildrenAliasBody(mp_Items, mp_Wrapped, m_bChoose);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  static void f_HelperEntryRemoveChildrenPointerAliasBody(CItems &_Items, CWrapped &_Wrapped, bool _bChoose) {
    auto *pAt = &_Items[0];
    (*pAt).m_Children.f_Remove(0);
  }
  TCFuture<int> f_HelperEntryRemoveChildrenPointerAlias() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      f_HelperEntryRemoveChildrenPointerAliasBody(mp_Items, mp_Wrapped, m_bChoose);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  static void f_HelperEntryChildRemoveChildrenPointerAliasBody(CItems &_Items, CWrapped &_Wrapped, bool _bChoose) {
    auto *pAt = &_Items[0];
    (*pAt).m_Children.f_Remove(0);
  }
  TCFuture<int> f_HelperEntryChildRemoveChildrenPointerAlias() {
    int *pEntry = mp_Items[0].m_Children.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items[0].m_Children.f_FindEqual(0);
      f_HelperEntryChildRemoveChildrenPointerAliasBody(mp_Items, mp_Wrapped, m_bChoose);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  static void f_HelperEntryRemoveChildrenIteratorBody(CItems &_Items, CWrapped &_Wrapped, bool _bChoose) {
    auto iEntry = _Items.f_GetIterator();
    (*iEntry).m_Children.f_Remove(0);
  }
  TCFuture<int> f_HelperEntryRemoveChildrenIterator() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      f_HelperEntryRemoveChildrenIteratorBody(mp_Items, mp_Wrapped, m_bChoose);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  static void f_HelperEntryChildRemoveChildrenIteratorBody(CItems &_Items, CWrapped &_Wrapped, bool _bChoose) {
    auto iEntry = _Items.f_GetIterator();
    (*iEntry).m_Children.f_Remove(0);
  }
  TCFuture<int> f_HelperEntryChildRemoveChildrenIterator() {
    int *pEntry = mp_Items[0].m_Children.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items[0].m_Children.f_FindEqual(0);
      f_HelperEntryChildRemoveChildrenIteratorBody(mp_Items, mp_Wrapped, m_bChoose);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  static void f_HelperEntryTidyChildrenSubscriptBody(CItems &_Items, CWrapped &_Wrapped, bool _bChoose) {
    _Items[0].m_Children.f_Tidy();
  }
  TCFuture<int> f_HelperEntryTidyChildrenSubscript() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      f_HelperEntryTidyChildrenSubscriptBody(mp_Items, mp_Wrapped, m_bChoose);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  static void f_HelperEntryChildTidyChildrenSubscriptBody(CItems &_Items, CWrapped &_Wrapped, bool _bChoose) {
    _Items[0].m_Children.f_Tidy();
  }
  TCFuture<int> f_HelperEntryChildTidyChildrenSubscript() {
    int *pEntry = mp_Items[0].m_Children.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items[0].m_Children.f_FindEqual(0);
      f_HelperEntryChildTidyChildrenSubscriptBody(mp_Items, mp_Wrapped, m_bChoose);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  static void f_HelperEntryTidyChildrenReferenceBody(CItems &_Items, CWrapped &_Wrapped, bool _bChoose) {
    _Items.f_Get(0).m_Children.f_Tidy();
  }
  TCFuture<int> f_HelperEntryTidyChildrenReference() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      f_HelperEntryTidyChildrenReferenceBody(mp_Items, mp_Wrapped, m_bChoose);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  static void f_HelperEntryChildTidyChildrenReferenceBody(CItems &_Items, CWrapped &_Wrapped, bool _bChoose) {
    _Items.f_Get(0).m_Children.f_Tidy();
  }
  TCFuture<int> f_HelperEntryChildTidyChildrenReference() {
    int *pEntry = mp_Items[0].m_Children.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items[0].m_Children.f_FindEqual(0);
      f_HelperEntryChildTidyChildrenReferenceBody(mp_Items, mp_Wrapped, m_bChoose);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  static void f_HelperEntryTidyChildrenPointerBody(CItems &_Items, CWrapped &_Wrapped, bool _bChoose) {
    (*_Items.f_FindEqual(0)).m_Children.f_Tidy();
  }
  TCFuture<int> f_HelperEntryTidyChildrenPointer() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      f_HelperEntryTidyChildrenPointerBody(mp_Items, mp_Wrapped, m_bChoose);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  static void f_HelperEntryChildTidyChildrenPointerBody(CItems &_Items, CWrapped &_Wrapped, bool _bChoose) {
    (*_Items.f_FindEqual(0)).m_Children.f_Tidy();
  }
  TCFuture<int> f_HelperEntryChildTidyChildrenPointer() {
    int *pEntry = mp_Items[0].m_Children.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items[0].m_Children.f_FindEqual(0);
      f_HelperEntryChildTidyChildrenPointerBody(mp_Items, mp_Wrapped, m_bChoose);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  static void f_HelperEntryTidyChildrenAliasBody(CItems &_Items, CWrapped &_Wrapped, bool _bChoose) {
    auto &Entry = _Items[0];
    Entry.m_Children.f_Tidy();
  }
  TCFuture<int> f_HelperEntryTidyChildrenAlias() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      f_HelperEntryTidyChildrenAliasBody(mp_Items, mp_Wrapped, m_bChoose);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  static void f_HelperEntryChildTidyChildrenAliasBody(CItems &_Items, CWrapped &_Wrapped, bool _bChoose) {
    auto &Entry = _Items[0];
    Entry.m_Children.f_Tidy();
  }
  TCFuture<int> f_HelperEntryChildTidyChildrenAlias() {
    int *pEntry = mp_Items[0].m_Children.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items[0].m_Children.f_FindEqual(0);
      f_HelperEntryChildTidyChildrenAliasBody(mp_Items, mp_Wrapped, m_bChoose);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  static void f_HelperEntryTidyChildrenPointerAliasBody(CItems &_Items, CWrapped &_Wrapped, bool _bChoose) {
    auto *pAt = &_Items[0];
    (*pAt).m_Children.f_Tidy();
  }
  TCFuture<int> f_HelperEntryTidyChildrenPointerAlias() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      f_HelperEntryTidyChildrenPointerAliasBody(mp_Items, mp_Wrapped, m_bChoose);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  static void f_HelperEntryChildTidyChildrenPointerAliasBody(CItems &_Items, CWrapped &_Wrapped, bool _bChoose) {
    auto *pAt = &_Items[0];
    (*pAt).m_Children.f_Tidy();
  }
  TCFuture<int> f_HelperEntryChildTidyChildrenPointerAlias() {
    int *pEntry = mp_Items[0].m_Children.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items[0].m_Children.f_FindEqual(0);
      f_HelperEntryChildTidyChildrenPointerAliasBody(mp_Items, mp_Wrapped, m_bChoose);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  static void f_HelperEntryTidyChildrenIteratorBody(CItems &_Items, CWrapped &_Wrapped, bool _bChoose) {
    auto iEntry = _Items.f_GetIterator();
    (*iEntry).m_Children.f_Tidy();
  }
  TCFuture<int> f_HelperEntryTidyChildrenIterator() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      f_HelperEntryTidyChildrenIteratorBody(mp_Items, mp_Wrapped, m_bChoose);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  static void f_HelperEntryChildTidyChildrenIteratorBody(CItems &_Items, CWrapped &_Wrapped, bool _bChoose) {
    auto iEntry = _Items.f_GetIterator();
    (*iEntry).m_Children.f_Tidy();
  }
  TCFuture<int> f_HelperEntryChildTidyChildrenIterator() {
    int *pEntry = mp_Items[0].m_Children.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items[0].m_Children.f_FindEqual(0);
      f_HelperEntryChildTidyChildrenIteratorBody(mp_Items, mp_Wrapped, m_bChoose);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  static void f_HelperEntryResetChildrenSubscriptBody(CItems &_Items, CWrapped &_Wrapped, bool _bChoose) {
    _Items[0].f_ResetChildren();
  }
  TCFuture<int> f_HelperEntryResetChildrenSubscript() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      f_HelperEntryResetChildrenSubscriptBody(mp_Items, mp_Wrapped, m_bChoose);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  static void f_HelperEntryChildResetChildrenSubscriptBody(CItems &_Items, CWrapped &_Wrapped, bool _bChoose) {
    _Items[0].f_ResetChildren();
  }
  TCFuture<int> f_HelperEntryChildResetChildrenSubscript() {
    int *pEntry = mp_Items[0].m_Children.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items[0].m_Children.f_FindEqual(0);
      f_HelperEntryChildResetChildrenSubscriptBody(mp_Items, mp_Wrapped, m_bChoose);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  static void f_HelperEntryResetChildrenReferenceBody(CItems &_Items, CWrapped &_Wrapped, bool _bChoose) {
    _Items.f_Get(0).f_ResetChildren();
  }
  TCFuture<int> f_HelperEntryResetChildrenReference() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      f_HelperEntryResetChildrenReferenceBody(mp_Items, mp_Wrapped, m_bChoose);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  static void f_HelperEntryChildResetChildrenReferenceBody(CItems &_Items, CWrapped &_Wrapped, bool _bChoose) {
    _Items.f_Get(0).f_ResetChildren();
  }
  TCFuture<int> f_HelperEntryChildResetChildrenReference() {
    int *pEntry = mp_Items[0].m_Children.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items[0].m_Children.f_FindEqual(0);
      f_HelperEntryChildResetChildrenReferenceBody(mp_Items, mp_Wrapped, m_bChoose);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  static void f_HelperEntryResetChildrenPointerBody(CItems &_Items, CWrapped &_Wrapped, bool _bChoose) {
    (*_Items.f_FindEqual(0)).f_ResetChildren();
  }
  TCFuture<int> f_HelperEntryResetChildrenPointer() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      f_HelperEntryResetChildrenPointerBody(mp_Items, mp_Wrapped, m_bChoose);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  static void f_HelperEntryChildResetChildrenPointerBody(CItems &_Items, CWrapped &_Wrapped, bool _bChoose) {
    (*_Items.f_FindEqual(0)).f_ResetChildren();
  }
  TCFuture<int> f_HelperEntryChildResetChildrenPointer() {
    int *pEntry = mp_Items[0].m_Children.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items[0].m_Children.f_FindEqual(0);
      f_HelperEntryChildResetChildrenPointerBody(mp_Items, mp_Wrapped, m_bChoose);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  static void f_HelperEntryResetChildrenAliasBody(CItems &_Items, CWrapped &_Wrapped, bool _bChoose) {
    auto &Entry = _Items[0];
    Entry.f_ResetChildren();
  }
  TCFuture<int> f_HelperEntryResetChildrenAlias() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      f_HelperEntryResetChildrenAliasBody(mp_Items, mp_Wrapped, m_bChoose);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  static void f_HelperEntryChildResetChildrenAliasBody(CItems &_Items, CWrapped &_Wrapped, bool _bChoose) {
    auto &Entry = _Items[0];
    Entry.f_ResetChildren();
  }
  TCFuture<int> f_HelperEntryChildResetChildrenAlias() {
    int *pEntry = mp_Items[0].m_Children.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items[0].m_Children.f_FindEqual(0);
      f_HelperEntryChildResetChildrenAliasBody(mp_Items, mp_Wrapped, m_bChoose);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  static void f_HelperEntryResetChildrenPointerAliasBody(CItems &_Items, CWrapped &_Wrapped, bool _bChoose) {
    auto *pAt = &_Items[0];
    (*pAt).f_ResetChildren();
  }
  TCFuture<int> f_HelperEntryResetChildrenPointerAlias() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      f_HelperEntryResetChildrenPointerAliasBody(mp_Items, mp_Wrapped, m_bChoose);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  static void f_HelperEntryChildResetChildrenPointerAliasBody(CItems &_Items, CWrapped &_Wrapped, bool _bChoose) {
    auto *pAt = &_Items[0];
    (*pAt).f_ResetChildren();
  }
  TCFuture<int> f_HelperEntryChildResetChildrenPointerAlias() {
    int *pEntry = mp_Items[0].m_Children.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items[0].m_Children.f_FindEqual(0);
      f_HelperEntryChildResetChildrenPointerAliasBody(mp_Items, mp_Wrapped, m_bChoose);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  static void f_HelperEntryResetChildrenIteratorBody(CItems &_Items, CWrapped &_Wrapped, bool _bChoose) {
    auto iEntry = _Items.f_GetIterator();
    (*iEntry).f_ResetChildren();
  }
  TCFuture<int> f_HelperEntryResetChildrenIterator() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      f_HelperEntryResetChildrenIteratorBody(mp_Items, mp_Wrapped, m_bChoose);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  static void f_HelperEntryChildResetChildrenIteratorBody(CItems &_Items, CWrapped &_Wrapped, bool _bChoose) {
    auto iEntry = _Items.f_GetIterator();
    (*iEntry).f_ResetChildren();
  }
  TCFuture<int> f_HelperEntryChildResetChildrenIterator() {
    int *pEntry = mp_Items[0].m_Children.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items[0].m_Children.f_FindEqual(0);
      f_HelperEntryChildResetChildrenIteratorBody(mp_Items, mp_Wrapped, m_bChoose);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  static void f_HelperEntryDeleteSubscriptBody(CItems &_Items, CWrapped &_Wrapped, bool _bChoose) {
    delete _Items[0].m_pNext;
  }
  TCFuture<int> f_HelperEntryDeleteSubscript() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      f_HelperEntryDeleteSubscriptBody(mp_Items, mp_Wrapped, m_bChoose);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  static void f_HelperEntryChildDeleteSubscriptBody(CItems &_Items, CWrapped &_Wrapped, bool _bChoose) {
    delete _Items[0].m_pNext;
  }
  TCFuture<int> f_HelperEntryChildDeleteSubscript() {
    int *pEntry = mp_Items[0].m_Children.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items[0].m_Children.f_FindEqual(0);
      f_HelperEntryChildDeleteSubscriptBody(mp_Items, mp_Wrapped, m_bChoose);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  static void f_HelperEntryDeleteReferenceBody(CItems &_Items, CWrapped &_Wrapped, bool _bChoose) {
    delete _Items.f_Get(0).m_pNext;
  }
  TCFuture<int> f_HelperEntryDeleteReference() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      f_HelperEntryDeleteReferenceBody(mp_Items, mp_Wrapped, m_bChoose);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  static void f_HelperEntryChildDeleteReferenceBody(CItems &_Items, CWrapped &_Wrapped, bool _bChoose) {
    delete _Items.f_Get(0).m_pNext;
  }
  TCFuture<int> f_HelperEntryChildDeleteReference() {
    int *pEntry = mp_Items[0].m_Children.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items[0].m_Children.f_FindEqual(0);
      f_HelperEntryChildDeleteReferenceBody(mp_Items, mp_Wrapped, m_bChoose);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  static void f_HelperEntryDeletePointerBody(CItems &_Items, CWrapped &_Wrapped, bool _bChoose) {
    delete (*_Items.f_FindEqual(0)).m_pNext;
  }
  TCFuture<int> f_HelperEntryDeletePointer() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      f_HelperEntryDeletePointerBody(mp_Items, mp_Wrapped, m_bChoose);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  static void f_HelperEntryChildDeletePointerBody(CItems &_Items, CWrapped &_Wrapped, bool _bChoose) {
    delete (*_Items.f_FindEqual(0)).m_pNext;
  }
  TCFuture<int> f_HelperEntryChildDeletePointer() {
    int *pEntry = mp_Items[0].m_Children.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items[0].m_Children.f_FindEqual(0);
      f_HelperEntryChildDeletePointerBody(mp_Items, mp_Wrapped, m_bChoose);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  static void f_HelperEntryDeleteAliasBody(CItems &_Items, CWrapped &_Wrapped, bool _bChoose) {
    auto &Entry = _Items[0];
    delete Entry.m_pNext;
  }
  TCFuture<int> f_HelperEntryDeleteAlias() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      f_HelperEntryDeleteAliasBody(mp_Items, mp_Wrapped, m_bChoose);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  static void f_HelperEntryChildDeleteAliasBody(CItems &_Items, CWrapped &_Wrapped, bool _bChoose) {
    auto &Entry = _Items[0];
    delete Entry.m_pNext;
  }
  TCFuture<int> f_HelperEntryChildDeleteAlias() {
    int *pEntry = mp_Items[0].m_Children.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items[0].m_Children.f_FindEqual(0);
      f_HelperEntryChildDeleteAliasBody(mp_Items, mp_Wrapped, m_bChoose);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  static void f_HelperEntryDeletePointerAliasBody(CItems &_Items, CWrapped &_Wrapped, bool _bChoose) {
    auto *pAt = &_Items[0];
    delete (*pAt).m_pNext;
  }
  TCFuture<int> f_HelperEntryDeletePointerAlias() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      f_HelperEntryDeletePointerAliasBody(mp_Items, mp_Wrapped, m_bChoose);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  static void f_HelperEntryChildDeletePointerAliasBody(CItems &_Items, CWrapped &_Wrapped, bool _bChoose) {
    auto *pAt = &_Items[0];
    delete (*pAt).m_pNext;
  }
  TCFuture<int> f_HelperEntryChildDeletePointerAlias() {
    int *pEntry = mp_Items[0].m_Children.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items[0].m_Children.f_FindEqual(0);
      f_HelperEntryChildDeletePointerAliasBody(mp_Items, mp_Wrapped, m_bChoose);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  static void f_HelperEntryDeleteIteratorBody(CItems &_Items, CWrapped &_Wrapped, bool _bChoose) {
    auto iEntry = _Items.f_GetIterator();
    delete (*iEntry).m_pNext;
  }
  TCFuture<int> f_HelperEntryDeleteIterator() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      f_HelperEntryDeleteIteratorBody(mp_Items, mp_Wrapped, m_bChoose);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  static void f_HelperEntryChildDeleteIteratorBody(CItems &_Items, CWrapped &_Wrapped, bool _bChoose) {
    auto iEntry = _Items.f_GetIterator();
    delete (*iEntry).m_pNext;
  }
  TCFuture<int> f_HelperEntryChildDeleteIterator() {
    int *pEntry = mp_Items[0].m_Children.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items[0].m_Children.f_FindEqual(0);
      f_HelperEntryChildDeleteIteratorBody(mp_Items, mp_Wrapped, m_bChoose);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  static void f_HelperEntryMoveSubscriptBody(CItems &_Items, CWrapped &_Wrapped, bool _bChoose) {
    CEntry Taken = fg_Move(_Items[0]);
  }
  TCFuture<int> f_HelperEntryMoveSubscript() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      f_HelperEntryMoveSubscriptBody(mp_Items, mp_Wrapped, m_bChoose);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  static void f_HelperEntryChildMoveSubscriptBody(CItems &_Items, CWrapped &_Wrapped, bool _bChoose) {
    CEntry Taken = fg_Move(_Items[0]);
  }
  TCFuture<int> f_HelperEntryChildMoveSubscript() {
    int *pEntry = mp_Items[0].m_Children.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items[0].m_Children.f_FindEqual(0);
      f_HelperEntryChildMoveSubscriptBody(mp_Items, mp_Wrapped, m_bChoose);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  static void f_HelperEntryMoveReferenceBody(CItems &_Items, CWrapped &_Wrapped, bool _bChoose) {
    CEntry Taken = fg_Move(_Items.f_Get(0));
  }
  TCFuture<int> f_HelperEntryMoveReference() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      f_HelperEntryMoveReferenceBody(mp_Items, mp_Wrapped, m_bChoose);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  static void f_HelperEntryChildMoveReferenceBody(CItems &_Items, CWrapped &_Wrapped, bool _bChoose) {
    CEntry Taken = fg_Move(_Items.f_Get(0));
  }
  TCFuture<int> f_HelperEntryChildMoveReference() {
    int *pEntry = mp_Items[0].m_Children.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items[0].m_Children.f_FindEqual(0);
      f_HelperEntryChildMoveReferenceBody(mp_Items, mp_Wrapped, m_bChoose);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  static void f_HelperEntryMovePointerBody(CItems &_Items, CWrapped &_Wrapped, bool _bChoose) {
    CEntry Taken = fg_Move((*_Items.f_FindEqual(0)));
  }
  TCFuture<int> f_HelperEntryMovePointer() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      f_HelperEntryMovePointerBody(mp_Items, mp_Wrapped, m_bChoose);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  static void f_HelperEntryChildMovePointerBody(CItems &_Items, CWrapped &_Wrapped, bool _bChoose) {
    CEntry Taken = fg_Move((*_Items.f_FindEqual(0)));
  }
  TCFuture<int> f_HelperEntryChildMovePointer() {
    int *pEntry = mp_Items[0].m_Children.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items[0].m_Children.f_FindEqual(0);
      f_HelperEntryChildMovePointerBody(mp_Items, mp_Wrapped, m_bChoose);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  static void f_HelperEntryMoveAliasBody(CItems &_Items, CWrapped &_Wrapped, bool _bChoose) {
    auto &Entry = _Items[0];
    CEntry Taken = fg_Move(Entry);
  }
  TCFuture<int> f_HelperEntryMoveAlias() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      f_HelperEntryMoveAliasBody(mp_Items, mp_Wrapped, m_bChoose);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  static void f_HelperEntryChildMoveAliasBody(CItems &_Items, CWrapped &_Wrapped, bool _bChoose) {
    auto &Entry = _Items[0];
    CEntry Taken = fg_Move(Entry);
  }
  TCFuture<int> f_HelperEntryChildMoveAlias() {
    int *pEntry = mp_Items[0].m_Children.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items[0].m_Children.f_FindEqual(0);
      f_HelperEntryChildMoveAliasBody(mp_Items, mp_Wrapped, m_bChoose);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  static void f_HelperEntryMovePointerAliasBody(CItems &_Items, CWrapped &_Wrapped, bool _bChoose) {
    auto *pAt = &_Items[0];
    CEntry Taken = fg_Move((*pAt));
  }
  TCFuture<int> f_HelperEntryMovePointerAlias() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      f_HelperEntryMovePointerAliasBody(mp_Items, mp_Wrapped, m_bChoose);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  static void f_HelperEntryChildMovePointerAliasBody(CItems &_Items, CWrapped &_Wrapped, bool _bChoose) {
    auto *pAt = &_Items[0];
    CEntry Taken = fg_Move((*pAt));
  }
  TCFuture<int> f_HelperEntryChildMovePointerAlias() {
    int *pEntry = mp_Items[0].m_Children.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items[0].m_Children.f_FindEqual(0);
      f_HelperEntryChildMovePointerAliasBody(mp_Items, mp_Wrapped, m_bChoose);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  static void f_HelperEntryMoveIteratorBody(CItems &_Items, CWrapped &_Wrapped, bool _bChoose) {
    auto iEntry = _Items.f_GetIterator();
    CEntry Taken = fg_Move((*iEntry));
  }
  TCFuture<int> f_HelperEntryMoveIterator() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      f_HelperEntryMoveIteratorBody(mp_Items, mp_Wrapped, m_bChoose);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  static void f_HelperEntryChildMoveIteratorBody(CItems &_Items, CWrapped &_Wrapped, bool _bChoose) {
    auto iEntry = _Items.f_GetIterator();
    CEntry Taken = fg_Move((*iEntry));
  }
  TCFuture<int> f_HelperEntryChildMoveIterator() {
    int *pEntry = mp_Items[0].m_Children.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items[0].m_Children.f_FindEqual(0);
      f_HelperEntryChildMoveIteratorBody(mp_Items, mp_Wrapped, m_bChoose);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  static void f_HelperEntryUpdateSubscriptBody(CItems &_Items, CWrapped &_Wrapped, bool _bChoose) {
    _Items[0].f_Update();
  }
  TCFuture<int> f_HelperEntryUpdateSubscript() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      f_HelperEntryUpdateSubscriptBody(mp_Items, mp_Wrapped, m_bChoose);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  static void f_HelperEntryChildUpdateSubscriptBody(CItems &_Items, CWrapped &_Wrapped, bool _bChoose) {
    _Items[0].f_Update();
  }
  TCFuture<int> f_HelperEntryChildUpdateSubscript() {
    int *pEntry = mp_Items[0].m_Children.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items[0].m_Children.f_FindEqual(0);
      f_HelperEntryChildUpdateSubscriptBody(mp_Items, mp_Wrapped, m_bChoose);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
  }
  static void f_HelperEntryUpdateReferenceBody(CItems &_Items, CWrapped &_Wrapped, bool _bChoose) {
    _Items.f_Get(0).f_Update();
  }
  TCFuture<int> f_HelperEntryUpdateReference() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      f_HelperEntryUpdateReferenceBody(mp_Items, mp_Wrapped, m_bChoose);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  static void f_HelperEntryChildUpdateReferenceBody(CItems &_Items, CWrapped &_Wrapped, bool _bChoose) {
    _Items.f_Get(0).f_Update();
  }
  TCFuture<int> f_HelperEntryChildUpdateReference() {
    int *pEntry = mp_Items[0].m_Children.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items[0].m_Children.f_FindEqual(0);
      f_HelperEntryChildUpdateReferenceBody(mp_Items, mp_Wrapped, m_bChoose);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
  }
  static void f_HelperEntryUpdatePointerBody(CItems &_Items, CWrapped &_Wrapped, bool _bChoose) {
    (*_Items.f_FindEqual(0)).f_Update();
  }
  TCFuture<int> f_HelperEntryUpdatePointer() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      f_HelperEntryUpdatePointerBody(mp_Items, mp_Wrapped, m_bChoose);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  static void f_HelperEntryChildUpdatePointerBody(CItems &_Items, CWrapped &_Wrapped, bool _bChoose) {
    (*_Items.f_FindEqual(0)).f_Update();
  }
  TCFuture<int> f_HelperEntryChildUpdatePointer() {
    int *pEntry = mp_Items[0].m_Children.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items[0].m_Children.f_FindEqual(0);
      f_HelperEntryChildUpdatePointerBody(mp_Items, mp_Wrapped, m_bChoose);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
  }
  static void f_HelperEntryUpdateAliasBody(CItems &_Items, CWrapped &_Wrapped, bool _bChoose) {
    auto &Entry = _Items[0];
    Entry.f_Update();
  }
  TCFuture<int> f_HelperEntryUpdateAlias() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      f_HelperEntryUpdateAliasBody(mp_Items, mp_Wrapped, m_bChoose);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  static void f_HelperEntryChildUpdateAliasBody(CItems &_Items, CWrapped &_Wrapped, bool _bChoose) {
    auto &Entry = _Items[0];
    Entry.f_Update();
  }
  TCFuture<int> f_HelperEntryChildUpdateAlias() {
    int *pEntry = mp_Items[0].m_Children.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items[0].m_Children.f_FindEqual(0);
      f_HelperEntryChildUpdateAliasBody(mp_Items, mp_Wrapped, m_bChoose);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
  }
  static void f_HelperEntryUpdatePointerAliasBody(CItems &_Items, CWrapped &_Wrapped, bool _bChoose) {
    auto *pAt = &_Items[0];
    (*pAt).f_Update();
  }
  TCFuture<int> f_HelperEntryUpdatePointerAlias() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      f_HelperEntryUpdatePointerAliasBody(mp_Items, mp_Wrapped, m_bChoose);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  static void f_HelperEntryChildUpdatePointerAliasBody(CItems &_Items, CWrapped &_Wrapped, bool _bChoose) {
    auto *pAt = &_Items[0];
    (*pAt).f_Update();
  }
  TCFuture<int> f_HelperEntryChildUpdatePointerAlias() {
    int *pEntry = mp_Items[0].m_Children.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items[0].m_Children.f_FindEqual(0);
      f_HelperEntryChildUpdatePointerAliasBody(mp_Items, mp_Wrapped, m_bChoose);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
  }
  static void f_HelperEntryUpdateIteratorBody(CItems &_Items, CWrapped &_Wrapped, bool _bChoose) {
    auto iEntry = _Items.f_GetIterator();
    (*iEntry).f_Update();
  }
  TCFuture<int> f_HelperEntryUpdateIterator() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      f_HelperEntryUpdateIteratorBody(mp_Items, mp_Wrapped, m_bChoose);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  static void f_HelperEntryChildUpdateIteratorBody(CItems &_Items, CWrapped &_Wrapped, bool _bChoose) {
    auto iEntry = _Items.f_GetIterator();
    (*iEntry).f_Update();
  }
  TCFuture<int> f_HelperEntryChildUpdateIterator() {
    int *pEntry = mp_Items[0].m_Children.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items[0].m_Children.f_FindEqual(0);
      f_HelperEntryChildUpdateIteratorBody(mp_Items, mp_Wrapped, m_bChoose);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
  }
  static void f_HelperEntryReplaceSubscriptBody(CItems &_Items, CWrapped &_Wrapped, bool _bChoose) {
    _Items[0].f_Renew();
  }
  TCFuture<int> f_HelperEntryReplaceSubscript() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      f_HelperEntryReplaceSubscriptBody(mp_Items, mp_Wrapped, m_bChoose);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  static void f_HelperEntryChildReplaceSubscriptBody(CItems &_Items, CWrapped &_Wrapped, bool _bChoose) {
    _Items[0].f_Renew();
  }
  TCFuture<int> f_HelperEntryChildReplaceSubscript() {
    int *pEntry = mp_Items[0].m_Children.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items[0].m_Children.f_FindEqual(0);
      f_HelperEntryChildReplaceSubscriptBody(mp_Items, mp_Wrapped, m_bChoose);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  static void f_HelperEntryReplaceReferenceBody(CItems &_Items, CWrapped &_Wrapped, bool _bChoose) {
    _Items.f_Get(0).f_Renew();
  }
  TCFuture<int> f_HelperEntryReplaceReference() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      f_HelperEntryReplaceReferenceBody(mp_Items, mp_Wrapped, m_bChoose);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  static void f_HelperEntryChildReplaceReferenceBody(CItems &_Items, CWrapped &_Wrapped, bool _bChoose) {
    _Items.f_Get(0).f_Renew();
  }
  TCFuture<int> f_HelperEntryChildReplaceReference() {
    int *pEntry = mp_Items[0].m_Children.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items[0].m_Children.f_FindEqual(0);
      f_HelperEntryChildReplaceReferenceBody(mp_Items, mp_Wrapped, m_bChoose);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  static void f_HelperEntryReplacePointerBody(CItems &_Items, CWrapped &_Wrapped, bool _bChoose) {
    (*_Items.f_FindEqual(0)).f_Renew();
  }
  TCFuture<int> f_HelperEntryReplacePointer() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      f_HelperEntryReplacePointerBody(mp_Items, mp_Wrapped, m_bChoose);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  static void f_HelperEntryChildReplacePointerBody(CItems &_Items, CWrapped &_Wrapped, bool _bChoose) {
    (*_Items.f_FindEqual(0)).f_Renew();
  }
  TCFuture<int> f_HelperEntryChildReplacePointer() {
    int *pEntry = mp_Items[0].m_Children.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items[0].m_Children.f_FindEqual(0);
      f_HelperEntryChildReplacePointerBody(mp_Items, mp_Wrapped, m_bChoose);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  static void f_HelperEntryReplaceAliasBody(CItems &_Items, CWrapped &_Wrapped, bool _bChoose) {
    auto &Entry = _Items[0];
    Entry.f_Renew();
  }
  TCFuture<int> f_HelperEntryReplaceAlias() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      f_HelperEntryReplaceAliasBody(mp_Items, mp_Wrapped, m_bChoose);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  static void f_HelperEntryChildReplaceAliasBody(CItems &_Items, CWrapped &_Wrapped, bool _bChoose) {
    auto &Entry = _Items[0];
    Entry.f_Renew();
  }
  TCFuture<int> f_HelperEntryChildReplaceAlias() {
    int *pEntry = mp_Items[0].m_Children.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items[0].m_Children.f_FindEqual(0);
      f_HelperEntryChildReplaceAliasBody(mp_Items, mp_Wrapped, m_bChoose);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  static void f_HelperEntryReplacePointerAliasBody(CItems &_Items, CWrapped &_Wrapped, bool _bChoose) {
    auto *pAt = &_Items[0];
    (*pAt).f_Renew();
  }
  TCFuture<int> f_HelperEntryReplacePointerAlias() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      f_HelperEntryReplacePointerAliasBody(mp_Items, mp_Wrapped, m_bChoose);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  static void f_HelperEntryChildReplacePointerAliasBody(CItems &_Items, CWrapped &_Wrapped, bool _bChoose) {
    auto *pAt = &_Items[0];
    (*pAt).f_Renew();
  }
  TCFuture<int> f_HelperEntryChildReplacePointerAlias() {
    int *pEntry = mp_Items[0].m_Children.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items[0].m_Children.f_FindEqual(0);
      f_HelperEntryChildReplacePointerAliasBody(mp_Items, mp_Wrapped, m_bChoose);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  static void f_HelperEntryReplaceIteratorBody(CItems &_Items, CWrapped &_Wrapped, bool _bChoose) {
    auto iEntry = _Items.f_GetIterator();
    (*iEntry).f_Renew();
  }
  TCFuture<int> f_HelperEntryReplaceIterator() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      f_HelperEntryReplaceIteratorBody(mp_Items, mp_Wrapped, m_bChoose);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  static void f_HelperEntryChildReplaceIteratorBody(CItems &_Items, CWrapped &_Wrapped, bool _bChoose) {
    auto iEntry = _Items.f_GetIterator();
    (*iEntry).f_Renew();
  }
  TCFuture<int> f_HelperEntryChildReplaceIterator() {
    int *pEntry = mp_Items[0].m_Children.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items[0].m_Children.f_FindEqual(0);
      f_HelperEntryChildReplaceIteratorBody(mp_Items, mp_Wrapped, m_bChoose);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }

  // In the helper: an iterator removing its entry, reported; stepping, which
  // assigns the iterator, not.
  static void f_HelperIteratorRemoveBody(CItems &_Items, CWrapped &_Wrapped, bool _bChoose) {
    auto iEntry = _Items.f_GetIterator();
    iEntry.f_Remove();
  }
  TCFuture<int> f_HelperIteratorRemove() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      f_HelperIteratorRemoveBody(mp_Items, mp_Wrapped, m_bChoose);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  static void f_HelperIteratorNextBody(CItems &_Items, CWrapped &_Wrapped, bool _bChoose) {
    auto iEntry = _Items.f_GetIterator();
    iEntry.f_Next();
  }
  TCFuture<int> f_HelperIteratorNext() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      f_HelperIteratorNextBody(mp_Items, mp_Wrapped, m_bChoose);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }

  // In a member function of the collection, through `this`, dereferenced,
  // cast or a local pointer to it, reported; an update of an entry or a step
  // of an iterator, not.
  TCFuture<int> f_MemberPruneDeref() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      mp_Items.f_PruneDeref();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_MemberPruneCast() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      mp_Items.f_PruneCast();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_MemberPrunePointer() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      mp_Items.f_PrunePointer();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_MemberUpdateFirst() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      mp_Items.f_UpdateFirst();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_MemberStep() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      mp_Items.f_Step();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }

  // A lookup through each form of the collection or of an entry, also looked
  // up again so in the callback, points into the member it goes through
  // first: removing from that one is reported, from another not.
  TCFuture<int> f_LookupDirectSame() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      mp_Items.f_Remove(0);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_LookupDirectOther() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      mp_Other.f_Remove(0);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_LookupCastSame() {
    CEntry *pEntry = static_cast<CItems &>(mp_Items).f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = static_cast<CItems &>(mp_Items).f_FindEqual(0);
      mp_Items.f_Remove(0);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_LookupCastOther() {
    CEntry *pEntry = static_cast<CItems &>(mp_Items).f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = static_cast<CItems &>(mp_Items).f_FindEqual(0);
      mp_Other.f_Remove(0);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_LookupConditionalSame() {
    CEntry *pEntry = (m_bChoose ? mp_Items : mp_Items).f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = (m_bChoose ? mp_Items : mp_Items).f_FindEqual(0);
      mp_Items.f_Remove(0);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_LookupConditionalOther() {
    CEntry *pEntry = (m_bChoose ? mp_Items : mp_Items).f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = (m_bChoose ? mp_Items : mp_Items).f_FindEqual(0);
      mp_Other.f_Remove(0);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_LookupCommaSame() {
    CEntry *pEntry = ((void)0, mp_Items).f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = ((void)0, mp_Items).f_FindEqual(0);
      mp_Items.f_Remove(0);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_LookupCommaOther() {
    CEntry *pEntry = ((void)0, mp_Items).f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = ((void)0, mp_Items).f_FindEqual(0);
      mp_Other.f_Remove(0);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_LookupReferenceSame() {
    auto &Items = mp_Items;
    CEntry *pEntry = Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      auto &Items = mp_Items;
      pEntry = Items.f_FindEqual(0);
      mp_Items.f_Remove(0);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_LookupReferenceOther() {
    auto &Items = mp_Items;
    CEntry *pEntry = Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      auto &Items = mp_Items;
      pEntry = Items.f_FindEqual(0);
      mp_Other.f_Remove(0);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_LookupPointerSame() {
    auto *pItems = &mp_Items;
    CEntry *pEntry = pItems->f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      auto *pItems = &mp_Items;
      pEntry = pItems->f_FindEqual(0);
      mp_Items.f_Remove(0);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_LookupPointerOther() {
    auto *pItems = &mp_Items;
    CEntry *pEntry = pItems->f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      auto *pItems = &mp_Items;
      pEntry = pItems->f_FindEqual(0);
      mp_Other.f_Remove(0);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_LookupThisPointerSame() {
    auto *pThis = this;
    CEntry *pEntry = pThis->mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      auto *pThis = this;
      pEntry = pThis->mp_Items.f_FindEqual(0);
      mp_Items.f_Remove(0);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_LookupThisPointerOther() {
    auto *pThis = this;
    CEntry *pEntry = pThis->mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      auto *pThis = this;
      pEntry = pThis->mp_Items.f_FindEqual(0);
      mp_Other.f_Remove(0);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_LookupThisDerefSame() {
    CEntry *pEntry = (*this).mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = (*this).mp_Items.f_FindEqual(0);
      mp_Items.f_Remove(0);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_LookupThisDerefOther() {
    CEntry *pEntry = (*this).mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = (*this).mp_Items.f_FindEqual(0);
      mp_Other.f_Remove(0);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_LookupSubobjectSame() {
    CEntry *pEntry = mp_Wrapped.m_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Wrapped.m_Items.f_FindEqual(0);
      mp_Wrapped.m_Items.f_Remove(0);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_LookupSubobjectOther() {
    CEntry *pEntry = mp_Wrapped.m_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Wrapped.m_Items.f_FindEqual(0);
      mp_Other.f_Remove(0);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_LookupSubscriptSame() {
    CEntry *pEntry = &mp_Items[0];
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = &mp_Items[0];
      mp_Items.f_Remove(0);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_LookupSubscriptOther() {
    CEntry *pEntry = &mp_Items[0];
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = &mp_Items[0];
      mp_Other.f_Remove(0);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_LookupReferenceGetterSame() {
    CEntry *pEntry = &mp_Items.f_Get(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = &mp_Items.f_Get(0);
      mp_Items.f_Remove(0);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_LookupReferenceGetterOther() {
    CEntry *pEntry = &mp_Items.f_Get(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = &mp_Items.f_Get(0);
      mp_Other.f_Remove(0);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_LookupEntryAliasSame() {
    auto &Entry = mp_Items[0];
    CEntry *pEntry = &Entry;
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      auto &Entry = mp_Items[0];
      pEntry = &Entry;
      mp_Items.f_Remove(0);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_LookupEntryAliasOther() {
    auto &Entry = mp_Items[0];
    CEntry *pEntry = &Entry;
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      auto &Entry = mp_Items[0];
      pEntry = &Entry;
      mp_Other.f_Remove(0);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_LookupEntryPointerAliasSame() {
    auto *pAt = &mp_Items[0];
    CEntry *pEntry = pAt;
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      auto *pAt = &mp_Items[0];
      pEntry = pAt;
      mp_Items.f_Remove(0);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_LookupEntryPointerAliasOther() {
    auto *pAt = &mp_Items[0];
    CEntry *pEntry = pAt;
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      auto *pAt = &mp_Items[0];
      pEntry = pAt;
      mp_Other.f_Remove(0);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_LookupIteratorSame() {
    auto iEntry = mp_Items.f_GetIterator();
    CEntry *pEntry = &*iEntry;
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      auto iEntry = mp_Items.f_GetIterator();
      pEntry = &*iEntry;
      mp_Items.f_Remove(0);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_LookupIteratorOther() {
    auto iEntry = mp_Items.f_GetIterator();
    CEntry *pEntry = &*iEntry;
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      auto iEntry = mp_Items.f_GetIterator();
      pEntry = &*iEntry;
      mp_Other.f_Remove(0);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }

  // An accessor that gives the collection itself, an element of a fixed-size
  // array, which is part of the object, a collection in an entry given to a
  // helper, also the entry, a member of a union assigned, the named
  // `operator*` of an owning pointer, a call through a pointer to a member
  // function, a virtual member, and an assignment through a pointer: each
  // reported when it changes the collection, not when it is a lookup, a
  // step or an update of data.
  static void fg_ClearChildren(CChildren &_Children) { _Children.f_Clear(); }
  static void fg_RemoveEntry(CEntry &_Entry) { _Entry.m_Children.f_Clear(); }
  static void fg_TouchEntry(CEntry &_Entry) { _Entry.m_Value = 1; }
  static void fg_EmptyUnion(CUnionItems &_Items) { _Items.m_Empty = 0; }
  static void fg_Visit(CVirtualItems &_Items) { _Items.f_Visit(); }
  static void fg_Count(CVirtualItems &_Items) { (void)_Items.f_Count(); }
  static void fg_VisitPointed(CVirtualItems &_Items) {
    auto fVisit = &CVirtualItems::f_Visit;
    (_Items.*fVisit)();
  }
  static void fg_ResetSlot(int &_Slot) { _Slot = 0; }
  static void fg_ClearFirst(CUnionItems &_Items) { _Items.m_pFirst = nullptr; }
  static void fg_Redirect(CUnionItems &_Items) {
    CUnionItems Other;
    auto *pItems = &_Items;
    pItems = &Other;
    (void)pItems;
  }
  static void fg_RunConst(CConstCall &_Items) { _Items(); }
  static void fg_ClearThrough(CItems &_Items) {
    auto *pItems = &_Items;
    fg_ClearIndirect(&pItems);
  }
  static void fg_ResetChildCount(CChildren &_Children) { _Children.m_Count = 0; }
  static void fg_ResetField(CUnionDerived &_Entry, int CUnionDerived::*_pField) {
    (_Entry.*_pField) = 0;
  }
  static void fg_ClearIndirectBody(CItems **_ppItems) { (*_ppItems)->f_Clear(); }
  static void fg_CleanOther(CItems &_Items) {
    constexpr bool c_bOther = true;
    (c_bOther ? g_OtherItems : _Items).f_Clear();
  }
  static void fg_RemoveStatic(CItems &_Items) { CRemover{}(_Items, 0); }
  static void fg_ResetCount(CUnionItems &_Items) { _Items.m_Count = 0; }
  static CItems &fg_Pick(CItems &_Items) {
    static CItems &Alias = _Items;
    return Alias;
  }
  static void fg_EmptyNested(CUnionItems &_Items) {
    _Items.m_Entry.m_Value = 0;
  }
  static void fg_PruneMoved(CItems &&_Items) { _Items.f_Remove(0); }
  static CItems &fg_ItemsOf(CWrapped &_Wrapped) {
    auto &Alias = _Wrapped;
    return Alias.m_Items;
  }
  static CItems *fg_Choose(CItems *_pFirst, CItems *_pSecond) {
    _pFirst = _pSecond;
    return _pFirst;
  }
  static void fg_ForwardEntry(CEntry &_Entry) { fg_ChangeEntry(_Entry); }
  static void fg_ReadEntry(CEntry &_Entry) { fg_Inspect(_Entry); }
  static void fg_ResetOwner(COwningEntry &_Entry) {
    _Entry.m_pItems = TCOwner<CItems>{};
  }
  static void fg_ClearThroughAlias(CItems &_Items) {
    auto *pItems = &_Items;
    auto ppItems = &pItems;
    fg_ClearIndirect(ppItems);
  }
  static void fg_PruneGeneric(CItems &_Items) {
    auto fPrune = [&](auto _Key) { _Items.f_Remove(_Key); };
    fPrune(0);
  }
  static void fg_ConsumeExplicit(CItems &_Items) {
    _Items.f_Rvalue().f_Consume();
  }
  static void fg_ConsumeOperator(CItems &_Items) { (void)!_Items.f_Rvalue(); }
  static void fg_ForwardNode(CNode &_Node) { fg_ChangeNode(_Node); }
  static void fg_ForwardRing(CRingA &_Ring) { fg_ChangeRing(_Ring); }
  static void fg_ForwardEntryPointer(CEntry *_pEntry) {
    fg_ChangeEntryPointer(_pEntry);
  }
  static void fg_ForwardUnionEntry(CUnionEntry &_Entry) {
    fg_ChangeUnionEntry(_Entry);
  }
  static void fg_WorkOn(CItems &_Items) { CWorker{}(_Items); }
  static void fg_ResetSlotPointer(int *&_pSlot) { _pSlot = nullptr; }
  static void fg_ResetSlotPointerAlias(int *&_pSlot) {
    auto &pAlias = _pSlot;
    pAlias = nullptr;
  }
  static void fg_ResetTemplateOwner(CTemplateOwningEntry &_Entry) {
    _Entry = CTemplateOwningEntry{};
  }
  static void fg_MakeInitClear(CItems &_Items) {
    CInitClear Made(_Items);
    (void)Made;
  }
  static void fg_OuterInitClear(CItems &_Items) { fg_MakeInitClear(_Items); }
  static void fg_MakeBaseClear(CItems &_Items) {
    CBaseClear Made(_Items);
    (void)Made;
  }
  static void fg_LocalClearOnExit(CItems &_Items) {
    CClearOnExit Cleanup{_Items};
    (void)Cleanup;
  }
  static void fg_OuterLocalClearOnExit(CItems &_Items) {
    fg_LocalClearOnExit(_Items);
  }
  static void fg_TemporaryClearOnExit(CItems &_Items) {
    (void)CClearOnExit{_Items};
  }
  static void fg_MemberClearOnExit(CItems &_Items) {
    CHolderOfClear Holder{{_Items}};
    (void)Holder;
  }
  static void fg_OpaqueEnd(CItems &_Items) {
    COpaqueEnd End{_Items};
    (void)End;
  }
  static void fg_Placed(CItems &_Items) { delete new (_Items) CPlaced; }
  static void fg_Destroy(CItems &_Items) { _Items.~CItems(); }
  static void fg_OuterDestroy(CItems &_Items) { fg_Destroy(_Items); }
  static void fg_ClearPointerCleanup(CItems **_ppItems) {
    (*_ppItems)->f_Clear();
  }
  static void fg_CleanupFunction(CItems &_Items) {
    CItems *pItems __attribute__((cleanup(fg_ClearPointerCleanup))) = &_Items;
    (void)pItems;
  }
  static void fg_ApplyVirtual(CProcessor &_Processor, CItems &_Items) {
    _Processor.f_Apply(_Items);
  }
  static void fg_ApplyQualified(CProcessor &_Processor, CItems &_Items) {
    _Processor.CProcessor::f_Apply(_Items);
  }
  static void fg_ApplyFinal(CFinalProcessor &_Processor, CItems &_Items) {
    _Processor.f_Apply(_Items);
  }
  static void fg_ApplyExact(CItems &_Items) {
    CProcessor Processor;
    Processor.f_Apply(_Items);
  }
  static void fg_Save(CItems &_Items) { g_pSaved = &_Items; }
  static void fg_SaveThenClear(CItems &_Items) {
    fg_Save(_Items);
    g_pSaved->f_Clear();
  }
  static void fg_KeepStatic(CItems &_Items) {
    static CItems *s_pItems = &_Items;
    (void)s_pItems;
  }
  static void fg_KeepLocal(CItems &_Items) {
    CItems *pLocal = &_Items;
    (void)pLocal;
  }
  static void fg_Redirect(CItems **_ppItems) { *_ppItems = &g_OtherItems; }
  static void fg_DrainThrough(CItems &_Items) {
    auto fDrain = &CItems::f_Drain;
    (_Items.f_Rvalue().*fDrain)();
  }
  static void fg_RemoveReassigned(CItems &_Items, CItems &_Other) {
    auto iEntry = _Other.f_GetIterator();
    iEntry = _Items.f_GetIterator();
    iEntry.f_Remove();
  }
  static void fg_RemoveStepped(CItems &_Items, CItems &_Other) {
    auto iEntry = _Other.f_GetIterator();
    ++iEntry;
    iEntry.f_Remove();
    (void)_Items;
  }
  static void fg_GuardBracesOwn(CItems &_Items) {
    CAggOwn Guard{_Items};
    (void)Guard;
  }
  static void fg_GuardBracesRef(CItems &_Items) {
    CAggRef Guard{_Items};
    (void)Guard;
  }
  static void fg_GuardBracesUnknown(CItems &_Items) {
    CAggUnknown Guard{_Items};
    (void)Guard;
  }
  static void fg_GuardParensOwn(CItems &_Items) {
    CAggOwn Guard(_Items);
    (void)Guard;
  }
  static void fg_GuardParensRef(CItems &_Items) {
    CAggRef Guard(_Items);
    (void)Guard;
  }
  static void fg_GuardParensUnknown(CItems &_Items) {
    CAggUnknown Guard(_Items);
    (void)Guard;
  }
  static void fg_GuardBaseOwn(CItems &_Items) {
    CDerivedOwn Guard{{_Items}};
    (void)Guard;
  }
  static void fg_GuardBaseRef(CItems &_Items) {
    CDerivedRef Guard{{_Items}};
    (void)Guard;
  }
  static void fg_GuardBaseUnknown(CItems &_Items) {
    CDerivedUnknown Guard{{_Items}};
    (void)Guard;
  }
  static void fg_GuardConstructorOwn(CItems &_Items) {
    CCtorOwn Guard(_Items);
    (void)Guard;
  }
  static void fg_GuardConstructorRef(CItems &_Items) {
    CCtorRef Guard(_Items);
    (void)Guard;
  }
  static void fg_GuardConstructorUnknown(CItems &_Items) {
    CCtorUnknown Guard(_Items);
    (void)Guard;
  }
  static void fg_GuardConstPointerOwn(CItems &_Items) {
    CItems *pItems = &_Items;
    CPtrOwn Guard(pItems);
    (void)Guard;
  }
  static void fg_GuardConstPointerRef(CItems &_Items) {
    CItems *pItems = &_Items;
    CPtrRef Guard(pItems);
    (void)Guard;
  }
  static void fg_GuardConstPointerUnknown(CItems &_Items) {
    CItems *pItems = &_Items;
    CPtrUnknown Guard(pItems);
    (void)Guard;
  }
  static void fg_GuardArrayOwn(CItems &_Items) {
    CAggOwn Guards[1] = {{_Items}};
    (void)Guards;
  }
  static void fg_GuardArrayRef(CItems &_Items) {
    CAggRef Guards[1] = {{_Items}};
    (void)Guards;
  }
  static void fg_GuardArrayUnknown(CItems &_Items) {
    CAggUnknown Guards[1] = {{_Items}};
    (void)Guards;
  }
  static void fg_GuardExtendedOwn(CItems &_Items) {
    const CAggOwn &Guard = CAggOwn{_Items};
    (void)Guard;
  }
  static void fg_GuardExtendedRef(CItems &_Items) {
    const CAggRef &Guard = CAggRef{_Items};
    (void)Guard;
  }
  static void fg_GuardExtendedUnknown(CItems &_Items) {
    const CAggUnknown &Guard = CAggUnknown{_Items};
    (void)Guard;
  }
  static void fg_GuardTemporaryOwn(CItems &_Items) {
    (void)CAggOwn{_Items};
  }
  static void fg_GuardTemporaryRef(CItems &_Items) {
    (void)CAggRef{_Items};
  }
  static void fg_GuardTemporaryUnknown(CItems &_Items) {
    (void)CAggUnknown{_Items};
  }
  static void fg_GuardFactoryOwn(CItems &_Items) {
    CFactoryOwn Factory;
    auto Guard = Factory.f_Make(_Items);
    (void)Guard;
  }
  static void fg_GuardFactoryRef(CItems &_Items) {
    CFactoryRef Factory;
    auto Guard = Factory.f_Make(_Items);
    (void)Guard;
  }
  static void fg_GuardFactoryUnknown(CItems &_Items) {
    CFactoryUnknown Factory;
    auto Guard = Factory.f_Make(_Items);
    (void)Guard;
  }
  static void fg_GuardOperatorFactoryOwn(CItems &_Items) {
    CFactoryOwn Factory;
    auto Guard = Factory(_Items, 0);
    (void)Guard;
  }
  static void fg_GuardOperatorFactoryRef(CItems &_Items) {
    CFactoryRef Factory;
    auto Guard = Factory(_Items, 0);
    (void)Guard;
  }
  static void fg_GuardOperatorFactoryUnknown(CItems &_Items) {
    CFactoryUnknown Factory;
    auto Guard = Factory(_Items, 0);
    (void)Guard;
  }
  static void fg_GuardReceiverOwn(CItems &_Items) {
    auto Guard = _Items.f_GuardOwn();
    (void)Guard;
  }
  static void fg_GuardReceiverRef(CItems &_Items) {
    auto Guard = _Items.f_GuardRef();
    (void)Guard;
  }
  static void fg_GuardReceiverUnknown(CItems &_Items) {
    auto Guard = _Items.f_GuardUnknown();
    (void)Guard;
  }
  static void fg_InnerSlot(CItems &_Items, CSlot &_Slot) {
    _Slot.m_pItems = &_Items;
    _Slot.m_pItems->f_Clear();
  }
  static void fg_OuterSlot(CItems &_Items) { fg_InnerSlot(_Items, g_Slot); }
  static void fg_ClearConstPointerRef(CItems *const &_pItems) {
    _pItems->f_Clear();
  }
  static void fg_ViewOperator(CItems &_Items) { (_Items + 0).f_Remove(0); }
  static void fg_ForgetCleanup(CItems **_ppItems) { *_ppItems = nullptr; }
  static void fg_CleanupForget(CItems &_Items) {
    CItems *pItems __attribute__((cleanup(fg_ForgetCleanup))) = &_Items;
    (void)pItems;
  }
  static void fg_CleanupReassigned(CItems &_First, CItems &_Last) {
    CItems *pItems __attribute__((cleanup(fg_ClearPointerCleanup))) = &_First;
    pItems = &_Last;
  }
  static void fg_ClearVisible(CVisibleItems &_Items) { _Items.f_Clear(); }
  static void fg_ClearTree(CTreeItems &_Items) { _Items.f_Clear(); }
  static void fg_LookTree(CTreeItems &_Items) { (void)_Items.f_FindEqual(1); }
  static void fg_LookVisible(CVisibleItems &_Items) {
    (void)_Items.f_FindEqual(1);
  }
  static void fg_ClearItems(CItems &_Items) { _Items.f_Clear(); }
  static void fg_LookItems(CItems &_Items) { (void)_Items.f_FindEqual(1); }
  static void fg_UnionGuard(CItems &_Items) {
    CUnionGuard Guard{.m_pItems = &_Items};
    (void)Guard;
  }
  static void fg_BitGuard(CItems &_Items) {
    CBitGuard Guard{&_Items};
    (void)Guard;
  }
  static void fg_ArrayGuard(CItems &_Items) {
    CArrayGuard Guard{{&_Items}};
    (void)Guard;
  }
  static void fg_LaterAssigned(CItems &_Items) {
    CLaterGuard Guard;
    Guard.m_pItems = &_Items;
  }
  static void fg_LaterSet(CItems &_Items) {
    CLaterGuard Guard;
    Guard.f_Set(_Items);
  }
  static void fg_LaterOwn(CItems &_Items) {
    CLaterOwnGuard Guard;
    Guard.m_pItems = &_Items;
  }
  static void fg_CleanupEarly(CItems &_Items, bool _bEarly) {
    CItems *pItems __attribute__((cleanup(fg_ClearPointerCleanup))) = &_Items;
    if (_bEarly)
      return;
    pItems = &g_OtherItems;
  }
  static void fg_AsmWrite(CItems &_Items) { asm volatile("" : "+m"(_Items)); }
  static void fg_AtomicWrite(CItems &_Items) {
    __atomic_store_n(&_Items.m_Count, 0, __ATOMIC_SEQ_CST);
  }
  static void fg_ForwardData(TCOwner<CHeapData> &_pData) {
    fg_ResetData(_pData);
  }
  TCFuture<int> f_RuleAccessorPrune() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      mp_Items.f_Entries().f_Prune();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleAccessorWithoutBody() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      mp_Items.f_Self().f_Prune();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleAccessorAssigned() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      auto &Items = mp_Items.f_Entries();
      Items = CItems{};
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleArrayElementAssigned() {
    CEntry *pEntry = mp_Array[0].f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Array[0].f_FindEqual(0);
      auto &Items = mp_Array[0];
      Items = CItems{};
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleArrayElementMoved() {
    CEntry *pEntry = mp_Array[0].f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Array[0].f_FindEqual(0);
      CItems Taken = fg_Move(mp_Array[0]);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleStdArrayElementAssigned() {
    CEntry *pEntry = mp_StdArray[0].f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_StdArray[0].f_FindEqual(0);
      auto &Items = mp_StdArray[0];
      Items = CItems{};
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleStdArrayElementMoved() {
    CEntry *pEntry = mp_StdArray[0].f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_StdArray[0].f_FindEqual(0);
      CItems Taken = fg_Move(mp_StdArray[0]);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleHelperClearsEntryCollection() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      fg_ClearChildren(mp_Items[0].m_Children);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_RuleHelperRemovesThroughEntry() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      fg_RemoveEntry(mp_Items[0]);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_RuleHelperTouchesEntry() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      fg_TouchEntry(mp_Items[0]);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_RuleUnionAssigned() {
    CEntry *pEntry = mp_Union.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Union.f_FindEqual(0);
      mp_Union.m_Empty = 0;
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleUnionAssignedInHelper() {
    CEntry *pEntry = mp_Union.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Union.f_FindEqual(0);
      fg_EmptyUnion(mp_Union);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleNamedDereference() {
    CEntry *pEntry = mp_pOwned->f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_pOwned->f_FindEqual(0);
      auto &Items = mp_pOwned.operator*();
      Items = CItems{};
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleDereference() {
    CEntry *pEntry = mp_pOwned->f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_pOwned->f_FindEqual(0);
      auto &Items = *mp_pOwned;
      Items = CItems{};
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleMemberPointerRemoving() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      mp_Items.f_ClearPointed();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleMemberPointerNotRemoving() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      mp_Items.f_TouchPointed();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_RuleMemberPointerUnknown() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      mp_Items.f_CallPointed(&CItems::f_Touch);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleMemberPointerInCallback() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      auto fClear = &CItems::f_Clear;
      (mp_Items.*fClear)();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleVirtualLookup() {
    CEntry *pEntry = mp_Virtual.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Virtual.f_FindEqual(0);
      (void)mp_Virtual.f_FindEqual(1);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_RuleVirtualMemberOfMember() {
    CEntry *pEntry = mp_Virtual.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Virtual.f_FindEqual(0);
      mp_Virtual.f_Visit();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_RuleVirtualMemberInHelper() {
    CEntry *pEntry = mp_Virtual.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Virtual.f_FindEqual(0);
      fg_Visit(mp_Virtual);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RulePointerReceiverAssigned() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      auto *pItems = &mp_Items;
      pItems->operator=(CItems{});
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleUnionNestedAssigned() {
    CEntry *pEntry = mp_Union.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Union.f_FindEqual(0);
      mp_Union.m_Entry.m_Value = 0;
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleUnionCompoundAssigned() {
    CEntry *pEntry = mp_Union.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Union.f_FindEqual(0);
      mp_Union.m_Empty += 1;
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleUnionCastAssigned() {
    CEntry *pEntry = mp_Union.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Union.f_FindEqual(0);
      reinterpret_cast<int &>(mp_Union.m_Entry.m_Value) = 0;
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleUnionNestedInHelper() {
    CEntry *pEntry = mp_Union.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Union.f_FindEqual(0);
      fg_EmptyNested(mp_Union);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleCollectionStateCompoundAssigned() {
    CEntry *pEntry = mp_Union.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Union.f_FindEqual(0);
      mp_Union.m_Count += 1;
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleOtherMemberAssigned() {
    CEntry *pEntry = mp_Union.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Union.f_FindEqual(0);
      m_bChoose = !m_bChoose;
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_RuleEntryCollectionRelocated() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      mp_Items[0].m_Children.f_Reserve(8);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_RuleCopyRemoved() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      mp_Items.f_Copy().f_Remove(0);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_RuleCopyGivenToHelper() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      fg_PruneMoved(mp_Items.f_Copy());
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_RuleMovedGivenToHelper() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      fg_PruneMoved(fg_Move(mp_Items));
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleVirtualConstInHelper() {
    CEntry *pEntry = mp_Virtual.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Virtual.f_FindEqual(0);
      fg_Count(mp_Virtual);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleVirtualPointedInHelper() {
    CEntry *pEntry = mp_Virtual.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Virtual.f_FindEqual(0);
      fg_VisitPointed(mp_Virtual);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleStdArrayAt() {
    CEntry *pEntry = mp_StdArray.at(0).f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_StdArray.at(0).f_FindEqual(0);
      auto &Items = mp_StdArray.at(0);
      Items = CItems{};
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleStdArrayFront() {
    CEntry *pEntry = mp_StdArray.front().f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_StdArray.front().f_FindEqual(0);
      auto &Items = mp_StdArray.front();
      Items = CItems{};
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleStdArrayBack() {
    CEntry *pEntry = mp_StdArray.back().f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_StdArray.back().f_FindEqual(0);
      auto &Items = mp_StdArray.back();
      Items = CItems{};
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleStdArrayData() {
    CEntry *pEntry = mp_StdArray.data()->f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_StdArray.data()->f_FindEqual(0);
      auto *pItems = mp_StdArray.data();
      *pItems = CItems{};
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleStdArrayNamedSubscript() {
    CEntry *pEntry = mp_StdArray.operator[](0).f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_StdArray.operator[](0).f_FindEqual(0);
      auto &Items = mp_StdArray.operator[](0);
      Items = CItems{};
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleHelperResultThroughAlias() {
    CEntry *pEntry = fg_ItemsOf(mp_Wrapped).f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = fg_ItemsOf(mp_Wrapped).f_FindEqual(0);
      fg_ItemsOf(mp_Wrapped).f_Remove(0);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleHelperResultReassigned() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      fg_Choose(&mp_Other, &mp_Items)->f_Remove(0);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleAddressOf() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      __builtin_addressof(mp_Items)->f_Remove(0);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleViewRemoved() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      mp_Items.f_View().f_Remove(0);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleUnionThroughAlias() {
    CEntry *pEntry = mp_Union.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Union.f_FindEqual(0);
      int &Slot = mp_Union.m_Empty;
      Slot = 0;
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleUnionThroughHelper() {
    CEntry *pEntry = mp_Union.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Union.f_FindEqual(0);
      fg_ResetSlot(mp_Union.m_Empty);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleConstBodyChanges() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      mp_Items.f_TidyConst();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleConstWithoutBody() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      (void)mp_Items.f_CountConst();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_RuleFixedArrayDataSubscript() {
    CEntry *pEntry = mp_Wrappers[0].m_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Wrappers[0].m_Items.f_FindEqual(0);
      mp_Wrappers.data()[0] = CWrapped{};
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleFixedArrayDataOffset() {
    CEntry *pEntry = mp_Wrappers[1].m_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Wrappers[1].m_Items.f_FindEqual(0);
      *(mp_Wrappers.data() + 1) = CWrapped{};
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleMemberPointerExactReceiver() {
    CEntry *pEntry = mp_Virtual.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Virtual.f_FindEqual(0);
      const auto fCount = &CVirtualItems::f_Count;
      (void)(mp_Virtual.*fCount)();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_RuleMemberPointerRelocates() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      auto fReserve = &CChildren::f_Reserve;
      (mp_Items[0].m_Children.*fReserve)(8);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_RuleEntryMovesItself() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      mp_Items[0].f_MoveOut();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_RuleMemberPointerChangedInLambda() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      mp_Items.f_ClearCaptured();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleHelperResultStaticAlias() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      fg_Pick(mp_Items).f_Clear();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleCopiedPointerReference() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      CItems *const &pItems = &mp_Items;
      [pItems] { pItems->f_Clear();
      }();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleHelperWritesState() {
    CEntry *pEntry = mp_Union.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Union.f_FindEqual(0);
      fg_ResetCount(mp_Union);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }

  // Each flag through each way relations are merged or stripped.
  TCFuture<int> f_RuleUnionConditional() {
    CUnionEntry *pEntry = mp_UnionEntries.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_UnionEntries.f_FindEqual(0);
      (m_bChoose ? mp_UnionEntries[0].m_Value : mp_UnionEntries[0].m_Empty) = 0;
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleUnionConditionalReversed() {
    CUnionEntry *pEntry = mp_UnionEntries.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_UnionEntries.f_FindEqual(0);
      (m_bChoose ? mp_UnionEntries[0].m_Empty : mp_UnionEntries[0].m_Value) = 0;
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleUnionComma() {
    CUnionEntry *pEntry = mp_UnionEntries.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_UnionEntries.f_FindEqual(0);
      ((void)0, mp_UnionEntries[0].m_Empty) = 0;
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleUnionCast() {
    CUnionEntry *pEntry = mp_UnionEntries.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_UnionEntries.f_FindEqual(0);
      reinterpret_cast<float &>(mp_UnionEntries[0].m_Empty) = 0;
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleUnionAlias() {
    CUnionEntry *pEntry = mp_UnionEntries.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_UnionEntries.f_FindEqual(0);
      int &Slot = mp_UnionEntries[0].m_Empty;
      Slot = 0;
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleUnionHelper() {
    CUnionEntry *pEntry = mp_UnionEntries.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_UnionEntries.f_FindEqual(0);
      fg_ResetSlot(mp_UnionEntries[0].m_Empty);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleUnionMemberPointer() {
    CUnionEntry *pEntry = mp_UnionEntries.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_UnionEntries.f_FindEqual(0);
      auto pmEmpty = &CUnionEntry::m_Empty;
      (mp_UnionEntries[0].*pmEmpty) = 0;
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleNotUnionMemberPointer() {
    CUnionEntry *pEntry = mp_UnionEntries.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_UnionEntries.f_FindEqual(0);
      auto pmValue = &CUnionEntry::m_Value;
      (mp_UnionEntries[0].*pmValue) = 0;
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_RuleNotUnionPlain() {
    CUnionEntry *pEntry = mp_UnionEntries.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_UnionEntries.f_FindEqual(0);
      mp_UnionEntries[0].m_Value = 0;
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_RuleCopyComma() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      ((void)0, mp_Items.f_Copy()).f_Remove(0);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_RuleCopyAlias() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      auto &&Copy = mp_Items.f_Copy();
      Copy.f_Remove(0);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_RuleViewComma() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      ((void)0, mp_Items.f_View()).f_Remove(0);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleViewAlias() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      auto &&View = mp_Items.f_View();
      View.f_Remove(0);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleReferenceView() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      mp_Items.f_RefView().f_Remove(0);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleAssignedThroughMemberPointer() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      mp_Items.f_AssignPointed();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RulePointerMemberWritten() {
    CEntry *pEntry = mp_Union.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Union.f_FindEqual(0);
      fg_ClearFirst(mp_Union);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleLocalPointerRedirected() {
    CEntry *pEntry = mp_Union.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Union.f_FindEqual(0);
      fg_Redirect(mp_Union);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_RuleConstCallOperatorWithoutBody() {
    CEntry *pEntry = mp_ConstCall.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_ConstCall.f_FindEqual(0);
      fg_RunConst(mp_ConstCall);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleAddressOfPointerForwarded() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      fg_ClearThrough(mp_Items);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleEntryCollectionStateInHelper() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      fg_ResetChildCount(mp_Items[0].m_Children);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_RuleEntryCollectionStateInMember() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      mp_Items[0].m_Children.f_ResetCount();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_RuleUnionThroughReassignedPointer() {
    CUnionEntry *pEntry = mp_UnionEntries.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_UnionEntries.f_FindEqual(0);
      mp_UnionEntries[0].f_WriteUnion();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleUnionInBaseThroughMemberPointer() {
    CUnionDerived *pEntry = mp_Derived.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Derived.f_FindEqual(0);
      fg_ResetField(mp_Derived[0], &CUnionDerived::m_Value);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RulePointerToPointerRoundTrip() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      CItems *const pItems = &mp_Items;
      auto ppItems = &pItems;
      (*ppItems)->f_Clear();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RulePointerToPointerHelper() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      auto *pItems = &mp_Items;
      fg_ClearIndirectBody(&pItems);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleConstantConditionChoosesOther() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      fg_CleanOther(mp_Items);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_RuleTaggedPointerResult() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      mp_Items.f_GetSelf()->f_Clear();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleSubscriptCopy() {
    CEntry *pEntry = mp_Values.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Values.f_FindEqual(0);
      mp_Values[0].m_Children.f_Tidy();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_RulePointerBackedElement() {
    CEntry *pEntry = mp_Union.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Union.f_FindEqual(0);
      mp_Union.f_TouchFirst();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_RuleStaticOperator() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      fg_RemoveStatic(mp_Items);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleEntryReplaced() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      mp_Items[0] = CEntry{};
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_RuleEntryCastToRvalue() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      CEntry Taken = static_cast<CEntry &&>(mp_Items[0]);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_RulePlainEntryMoved() {
    CUnionEntry *pEntry = mp_UnionEntries.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_UnionEntries.f_FindEqual(0);
      CUnionEntry Taken = fg_Move(mp_UnionEntries[0]);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_RulePlainEntryCastToRvalue() {
    CUnionEntry *pEntry = mp_UnionEntries.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_UnionEntries.f_FindEqual(0);
      CUnionEntry Taken = static_cast<CUnionEntry &&>(mp_UnionEntries[0]);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }

  // What changes the collection an entry holds, a lookup in that collection.
  TCFuture<int> f_RuleHelperClearsEntryCollectionChild() {
    int *pEntry = mp_Items[0].m_Children.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items[0].m_Children.f_FindEqual(0);
      fg_ClearChildren(mp_Items[0].m_Children);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleHelperRemovesThroughEntryChild() {
    int *pEntry = mp_Items[0].m_Children.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items[0].m_Children.f_FindEqual(0);
      fg_RemoveEntry(mp_Items[0]);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleHelperTouchesEntryChild() {
    int *pEntry = mp_Items[0].m_Children.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items[0].m_Children.f_FindEqual(0);
      fg_TouchEntry(mp_Items[0]);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
  }
  TCFuture<int> f_RuleEntryCollectionRelocatedChild() {
    int *pEntry = mp_Items[0].m_Children.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items[0].m_Children.f_FindEqual(0);
      mp_Items[0].m_Children.f_Reserve(8);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleMemberPointerRelocatesChild() {
    int *pEntry = mp_Items[0].m_Children.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items[0].m_Children.f_FindEqual(0);
      auto fReserve = &CChildren::f_Reserve;
      (mp_Items[0].m_Children.*fReserve)(8);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleEntryMovesItselfChild() {
    int *pEntry = mp_Items[0].m_Children.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items[0].m_Children.f_FindEqual(0);
      mp_Items[0].f_MoveOut();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleEntryCollectionStateInHelperChild() {
    int *pEntry = mp_Items[0].m_Children.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items[0].m_Children.f_FindEqual(0);
      fg_ResetChildCount(mp_Items[0].m_Children);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleEntryCollectionStateInMemberChild() {
    int *pEntry = mp_Items[0].m_Children.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items[0].m_Children.f_FindEqual(0);
      mp_Items[0].m_Children.f_ResetCount();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleEntryReplacedChild() {
    int *pEntry = mp_Items[0].m_Children.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items[0].m_Children.f_FindEqual(0);
      mp_Items[0] = CEntry{};
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleEntryCastToRvalueChild() {
    int *pEntry = mp_Items[0].m_Children.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items[0].m_Children.f_FindEqual(0);
      CEntry Taken = static_cast<CEntry &&>(mp_Items[0]);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleEntryGivenToUnknown() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      fg_ChangeEntry(mp_Items[0]);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleEntryGivenToUnknownChild() {
    int *pEntry = mp_Items[0].m_Children.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items[0].m_Children.f_FindEqual(0);
      fg_ChangeEntry(mp_Items[0]);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleEntryForwardedInHelper() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      fg_ForwardEntry(mp_Items[0]);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleEntryForwardedInHelperChild() {
    int *pEntry = mp_Items[0].m_Children.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items[0].m_Children.f_FindEqual(0);
      fg_ForwardEntry(mp_Items[0]);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleEntryForwardedConstInHelperChild() {
    int *pEntry = mp_Items[0].m_Children.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items[0].m_Children.f_FindEqual(0);
      fg_ReadEntry(mp_Items[0]);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
  }
  TCFuture<int> f_RuleOwningPointerOfEntryReplaced() {
    CEntry *pEntry = mp_OwningEntries[0].m_pItems->f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_OwningEntries[0].m_pItems->f_FindEqual(0);
      mp_OwningEntries[0].m_pItems = TCOwner<CItems>{};
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleOwningPointerOfEntryReplacedInHelper() {
    CEntry *pEntry = mp_OwningEntries[0].m_pItems->f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_OwningEntries[0].m_pItems->f_FindEqual(0);
      fg_ResetOwner(mp_OwningEntries[0]);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleOwningEntryReplaced() {
    CEntry *pEntry = mp_OwningEntries[0].m_pItems->f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_OwningEntries[0].m_pItems->f_FindEqual(0);
      mp_OwningEntries[0] = COwningEntry{};
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleOwningEntryUpdated() {
    CEntry *pEntry = mp_OwningEntries[0].m_pItems->f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_OwningEntries[0].m_pItems->f_FindEqual(0);
      mp_OwningEntries[0].m_Value = 1;
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }

  // A local pointer into a union pointed elsewhere, the address of a local
  // pointer through a local made with it, a generic lambda, a consuming call
  // with an explicit object parameter or as an operator, a conditional of a
  // member and an entry, an owning pointer in a union, and what a helper
  // without a body gives of a member of a union.
  TCFuture<int> f_RuleUnionLocalPointerRedirected() {
    CUnionEntry *pEntry = mp_UnionEntries.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_UnionEntries.f_FindEqual(0);
      int Other = 0;
      int *pSlot = &mp_UnionEntries[0].m_Empty;
      pSlot = &Other;
      (void)pSlot;
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_RuleAddressAliasForwarded() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      auto *pItems = &mp_Items;
      auto ppItems = &pItems;
      fg_ClearIndirectBody(ppItems);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleAddressAliasForwardedInHelper() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      fg_ClearThroughAlias(mp_Items);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleGenericLambdaInHelper() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      fg_PruneGeneric(mp_Items);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleGenericLambdaInCallback() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      auto fPrune = [&](auto _Key) { mp_Items.f_Remove(_Key);
      };
      fPrune(0);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleConsumedExplicit() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      mp_Items.f_Rvalue().f_Consume();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleConsumedOperator() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      (void)!mp_Items.f_Rvalue();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleConsumedExplicitDeclared() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      mp_Items.f_RvalueDeclared().f_Consume();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleConsumedOperatorDeclared() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      (void)!mp_Items.f_RvalueDeclared();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleConsumedExplicitInHelper() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      fg_ConsumeExplicit(mp_Items);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleConsumedOperatorInHelper() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      fg_ConsumeOperator(mp_Items);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleMixedMemberAndEntryWritten() {
    CEntry *pEntry = mp_Union.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Union.f_FindEqual(0);
      (m_bChoose ? mp_Union.m_Count : mp_Items[0].m_Value) = 0;
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleUnionOwnedPointee() {
    CEntry *pEntry = mp_Union.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Union.f_FindEqual(0);
      mp_Union.m_pOwnedItems->f_Get(0).m_Value = 0;
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_RuleUnionThroughUnknownHelper() {
    CUnionEntry *pEntry = mp_UnionEntries.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_UnionEntries.f_FindEqual(0);
      fg_PassSlot(mp_UnionEntries[0].m_Empty) = 0;
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleUnionThroughAliasHelper() {
    CUnionEntry *pEntry = mp_UnionEntries.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_UnionEntries.f_FindEqual(0);
      const_cast<int &>(fg_AliasConst(mp_UnionEntries[0].m_Empty)) = 0;
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleUnionThroughAliasHelperInMember() {
    CUnionEntry *pEntry = mp_UnionEntries.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_UnionEntries.f_FindEqual(0);
      mp_UnionEntries[0].f_WriteThroughAlias();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }

  // Recursive and inherited owners, pointers to entries, and what else
  // classifies an entry's change by what it holds.
  TCFuture<int> f_RuleSelfOwningReplaced() {
    CNode *pEntry = mp_Nodes.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Nodes.f_FindEqual(0);
      mp_Nodes[0] = CNode{};
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_RuleSelfOwningGivenOn() {
    CNode *pEntry = mp_Nodes.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Nodes.f_FindEqual(0);
      fg_ChangeNode(mp_Nodes[0]);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleSelfOwningForwarded() {
    CNode *pEntry = mp_Nodes.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Nodes.f_FindEqual(0);
      fg_ForwardNode(mp_Nodes[0]);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleSelfOwningPointeeReplaced() {
    int *pEntry = &mp_Nodes[0].m_pNext->m_Value;
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = &mp_Nodes[0].m_pNext->m_Value;
      mp_Nodes[0].m_pNext = TCOwner<CNode>{};
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleSelfOwningPointeeForwarded() {
    int *pEntry = &mp_Nodes[0].m_pNext->m_Value;
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = &mp_Nodes[0].m_pNext->m_Value;
      fg_ForwardNode(mp_Nodes[0]);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleRingReplaced() {
    CRingA *pEntry = mp_Rings.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Rings.f_FindEqual(0);
      mp_Rings[0] = CRingA{};
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_RuleRingForwarded() {
    CRingA *pEntry = mp_Rings.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Rings.f_FindEqual(0);
      fg_ForwardRing(mp_Rings[0]);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleRingPointeeReplaced() {
    int *pEntry = &mp_Rings[0].m_pB->m_pA->m_Value;
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = &mp_Rings[0].m_pB->m_pA->m_Value;
      mp_Rings[0].m_pB = TCOwner<CRingB>{};
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleInheritedOwnerGivenOn() {
    CEntry *pEntry = mp_Inherited[0].m_pItems->f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Inherited[0].m_pItems->f_FindEqual(0);
      fg_ChangeInherited(mp_Inherited[0]);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleInheritedOwnerReplaced() {
    CEntry *pEntry = mp_Inherited[0].m_pItems->f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Inherited[0].m_pItems->f_FindEqual(0);
      mp_Inherited[0].m_pItems = TCDerivedOwner<CItems>{};
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleInheritedOwnerEntryGivenOn() {
    CInheritedOwningEntry *pEntry = mp_Inherited.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Inherited.f_FindEqual(0);
      fg_ChangeInherited(mp_Inherited[0]);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleStarOwnerGivenOn() {
    CEntry *pEntry = (*mp_Star[0].m_pItems).f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = (*mp_Star[0].m_pItems).f_FindEqual(0);
      fg_ChangeStar(mp_Star[0]);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleEntryPointerForwardedInHelperChild() {
    int *pEntry = mp_Items[0].m_Children.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items[0].m_Children.f_FindEqual(0);
      fg_ForwardEntryPointer(&mp_Items[0]);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleEntryPointerForwardedInHelper() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      fg_ForwardEntryPointer(&mp_Items[0]);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleEntryThisForwardedChild() {
    int *pEntry = mp_Items[0].m_Children.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items[0].m_Children.f_FindEqual(0);
      mp_Items[0].f_ChangeThroughPointer();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleEntryThisForwarded() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      mp_Items[0].f_ChangeThroughPointer();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleConsumedCallGivenCollectionInHelper() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      fg_WorkOn(mp_Items);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleConsumedCallGivenCollection() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      CWorker{}(mp_Items);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleEntryRemovesItself() {
    CSelfRemoving *pEntry = mp_SelfRemoving.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_SelfRemoving.f_FindEqual(0);
      mp_SelfRemoving[0].f_Remove();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleVirtualEntryChangeChild() {
    int *pEntry = mp_VirtualEntries[0].m_Children.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_VirtualEntries[0].m_Children.f_FindEqual(0);
      mp_VirtualEntries.f_FindEqual(1)->f_Alter();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleVirtualEntryChange() {
    CVirtualEntry *pEntry = mp_VirtualEntries.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_VirtualEntries.f_FindEqual(0);
      mp_VirtualEntries.f_FindEqual(1)->f_Alter();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleUnionEntryForwardedInHelper() {
    CUnionEntry *pEntry = mp_UnionEntries.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_UnionEntries.f_FindEqual(0);
      fg_ForwardUnionEntry(mp_UnionEntries[0]);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleNumberMovedInMember() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      (void)mp_Items.f_CountMoved();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_RuleNestedChangeInMember() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      mp_Items.f_TidyNested();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_RuleNestedChangeInMemberChild() {
    int *pEntry = mp_Items[0].m_Children.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items[0].m_Children.f_FindEqual(0);
      mp_Items.f_TidyNested();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleOwnedValueReplaced() {
    int *pEntry = &mp_Heap[0].m_pValue->m_Value;
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = &mp_Heap[0].m_pValue->m_Value;
      mp_Heap[0].m_pValue = TCOwner<CHeapValue>{};
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleOwnedValueReplacedEntry() {
    CHeapEntry *pEntry = mp_Heap.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Heap.f_FindEqual(0);
      mp_Heap[0].m_pValue = TCOwner<CHeapValue>{};
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_RuleOwnedValueCleared() {
    int *pEntry = &mp_Heap[0].m_pClearable->m_Value;
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = &mp_Heap[0].m_pClearable->m_Value;
      mp_Heap[0].m_pClearable.f_Clear();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleOwnedValueClearedEntry() {
    CHeapEntry *pEntry = mp_Heap.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Heap.f_FindEqual(0);
      mp_Heap[0].m_pClearable.f_Clear();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_RuleSelfAddressAccessor() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      mp_Items.f_SelfAddress()->f_Clear();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleSelfAliasAccessor() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      mp_Items.f_SelfAlias()->f_Clear();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleSelfExplicitAddressAccessor() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      mp_Items.f_SelfExplicitAddress()->f_Clear();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleSelfReferenceAccessor() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      mp_Items.f_SelfReference().f_Clear();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleSelfIndexed() {
    CEntry *pEntry = mp_SelfIndexed.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_SelfIndexed.f_FindEqual(0);
      mp_SelfIndexed[0].f_Clear();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleSubobjectGetterChild() {
    int *pEntry = mp_Items[0].m_Children.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items[0].m_Children.f_FindEqual(0);
      mp_Items[0].f_Children().f_Clear();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleSubobjectGetter() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      mp_Items[0].f_Children().f_Clear();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_RuleFixedArrayBegin() {
    CEntry *pEntry = mp_StdArray[0].f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_StdArray[0].f_FindEqual(0);
      mp_StdArray.begin()->f_Clear();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }

  // Each depth through each path, looked up and changed.
  TCFuture<int> f_DepthEntryDirectRemoved() {
    int *pEntry = &mp_Items[0].m_Value;
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = &mp_Items[0].m_Value;
      mp_Items.f_Remove(0);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_DepthEntryDirectChildrenCleared() {
    int *pEntry = &mp_Items[0].m_Value;
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = &mp_Items[0].m_Value;
      mp_Items[0].m_Children.f_Clear();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
  }
  TCFuture<int> f_DepthEntryDirectDataReset() {
    int *pEntry = &mp_Items[0].m_Value;
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = &mp_Items[0].m_Value;
      mp_Items[0].m_pData = TCOwner<CHeapData>{};
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
  }
  TCFuture<int> f_DepthChildDirectCleared() {
    int *pEntry = mp_Items[0].m_Children.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items[0].m_Children.f_FindEqual(0);
      mp_Items[0].m_Children.f_Clear();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_DepthChildDirectDataReset() {
    int *pEntry = mp_Items[0].m_Children.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items[0].m_Children.f_FindEqual(0);
      mp_Items[0].m_pData = TCOwner<CHeapData>{};
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_DepthOwnedDirectDataReset() {
    int *pEntry = &mp_Items[0].m_pData->m_Value;
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = &mp_Items[0].m_pData->m_Value;
      mp_Items[0].m_pData = TCOwner<CHeapData>{};
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_DepthOwnedDirectChildrenCleared() {
    int *pEntry = &mp_Items[0].m_pData->m_Value;
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = &mp_Items[0].m_pData->m_Value;
      mp_Items[0].m_Children.f_Clear();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_DepthChildClearedThroughDirect() {
    int *pEntry = mp_Items[0].m_Children.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items[0].m_Children.f_FindEqual(0);
      mp_Items[0].m_Children.f_Clear();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_DepthOwnedResetThroughDirect() {
    int *pEntry = &mp_Items[0].m_pData->m_Value;
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = &mp_Items[0].m_pData->m_Value;
      mp_Items[0].m_pData = TCOwner<CHeapData>{};
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_DepthEntryKeptThroughDirect() {
    int *pEntry = &mp_Items[0].m_Value;
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = &mp_Items[0].m_Value;
      mp_Items[0].m_pData = TCOwner<CHeapData>{};
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
  }
  TCFuture<int> f_DepthEntryAccessorRemoved() {
    int *pEntry = &mp_Items.f_EntryAt(0).m_Value;
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = &mp_Items.f_EntryAt(0).m_Value;
      mp_Items.f_Remove(0);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_DepthEntryAccessorChildrenCleared() {
    int *pEntry = &mp_Items.f_EntryAt(0).m_Value;
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = &mp_Items.f_EntryAt(0).m_Value;
      mp_Items[0].m_Children.f_Clear();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
  }
  TCFuture<int> f_DepthEntryAccessorDataReset() {
    int *pEntry = &mp_Items.f_EntryAt(0).m_Value;
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = &mp_Items.f_EntryAt(0).m_Value;
      mp_Items[0].m_pData = TCOwner<CHeapData>{};
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
  }
  TCFuture<int> f_DepthChildAccessorCleared() {
    int *pEntry = mp_Items.f_EntryAt(0).m_Children.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_EntryAt(0).m_Children.f_FindEqual(0);
      mp_Items[0].m_Children.f_Clear();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_DepthChildAccessorDataReset() {
    int *pEntry = mp_Items.f_EntryAt(0).m_Children.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_EntryAt(0).m_Children.f_FindEqual(0);
      mp_Items[0].m_pData = TCOwner<CHeapData>{};
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_DepthOwnedAccessorDataReset() {
    int *pEntry = &mp_Items.f_EntryAt(0).m_pData->m_Value;
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = &mp_Items.f_EntryAt(0).m_pData->m_Value;
      mp_Items[0].m_pData = TCOwner<CHeapData>{};
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_DepthOwnedAccessorChildrenCleared() {
    int *pEntry = &mp_Items.f_EntryAt(0).m_pData->m_Value;
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = &mp_Items.f_EntryAt(0).m_pData->m_Value;
      mp_Items[0].m_Children.f_Clear();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_DepthChildClearedThroughAccessor() {
    int *pEntry = mp_Items[0].m_Children.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items[0].m_Children.f_FindEqual(0);
      mp_Items.f_EntryAt(0).m_Children.f_Clear();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_DepthOwnedResetThroughAccessor() {
    int *pEntry = &mp_Items[0].m_pData->m_Value;
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = &mp_Items[0].m_pData->m_Value;
      mp_Items.f_EntryAt(0).m_pData = TCOwner<CHeapData>{};
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_DepthEntryKeptThroughAccessor() {
    int *pEntry = &mp_Items[0].m_Value;
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = &mp_Items[0].m_Value;
      mp_Items.f_EntryAt(0).m_pData = TCOwner<CHeapData>{};
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
  }
  TCFuture<int> f_DepthEntryAliasRemoved() {
    auto &Entry = mp_Items[0];
    int *pEntry = &Entry.m_Value;
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      auto &Entry = mp_Items[0];
      pEntry = &Entry.m_Value;
      mp_Items.f_Remove(0);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_DepthEntryAliasChildrenCleared() {
    auto &Entry = mp_Items[0];
    int *pEntry = &Entry.m_Value;
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      auto &Entry = mp_Items[0];
      pEntry = &Entry.m_Value;
      mp_Items[0].m_Children.f_Clear();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
  }
  TCFuture<int> f_DepthEntryAliasDataReset() {
    auto &Entry = mp_Items[0];
    int *pEntry = &Entry.m_Value;
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      auto &Entry = mp_Items[0];
      pEntry = &Entry.m_Value;
      mp_Items[0].m_pData = TCOwner<CHeapData>{};
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
  }
  TCFuture<int> f_DepthChildAliasCleared() {
    auto &Entry = mp_Items[0];
    int *pEntry = Entry.m_Children.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      auto &Entry = mp_Items[0];
      pEntry = Entry.m_Children.f_FindEqual(0);
      mp_Items[0].m_Children.f_Clear();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_DepthChildAliasDataReset() {
    auto &Entry = mp_Items[0];
    int *pEntry = Entry.m_Children.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      auto &Entry = mp_Items[0];
      pEntry = Entry.m_Children.f_FindEqual(0);
      mp_Items[0].m_pData = TCOwner<CHeapData>{};
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_DepthOwnedAliasDataReset() {
    auto &Entry = mp_Items[0];
    int *pEntry = &Entry.m_pData->m_Value;
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      auto &Entry = mp_Items[0];
      pEntry = &Entry.m_pData->m_Value;
      mp_Items[0].m_pData = TCOwner<CHeapData>{};
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_DepthOwnedAliasChildrenCleared() {
    auto &Entry = mp_Items[0];
    int *pEntry = &Entry.m_pData->m_Value;
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      auto &Entry = mp_Items[0];
      pEntry = &Entry.m_pData->m_Value;
      mp_Items[0].m_Children.f_Clear();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_DepthChildClearedThroughAlias() {
    auto &Entry = mp_Items[0];
    int *pEntry = mp_Items[0].m_Children.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      auto &Entry = mp_Items[0];
      pEntry = mp_Items[0].m_Children.f_FindEqual(0);
      Entry.m_Children.f_Clear();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_DepthOwnedResetThroughAlias() {
    auto &Entry = mp_Items[0];
    int *pEntry = &mp_Items[0].m_pData->m_Value;
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      auto &Entry = mp_Items[0];
      pEntry = &mp_Items[0].m_pData->m_Value;
      Entry.m_pData = TCOwner<CHeapData>{};
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_DepthEntryKeptThroughAlias() {
    auto &Entry = mp_Items[0];
    int *pEntry = &mp_Items[0].m_Value;
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      auto &Entry = mp_Items[0];
      pEntry = &mp_Items[0].m_Value;
      Entry.m_pData = TCOwner<CHeapData>{};
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
  }
  TCFuture<int> f_DepthEntryCastRemoved() {
    int *pEntry = &static_cast<CEntry &>(mp_Items[0]).m_Value;
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = &static_cast<CEntry &>(mp_Items[0]).m_Value;
      mp_Items.f_Remove(0);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_DepthEntryCastChildrenCleared() {
    int *pEntry = &static_cast<CEntry &>(mp_Items[0]).m_Value;
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = &static_cast<CEntry &>(mp_Items[0]).m_Value;
      mp_Items[0].m_Children.f_Clear();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
  }
  TCFuture<int> f_DepthEntryCastDataReset() {
    int *pEntry = &static_cast<CEntry &>(mp_Items[0]).m_Value;
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = &static_cast<CEntry &>(mp_Items[0]).m_Value;
      mp_Items[0].m_pData = TCOwner<CHeapData>{};
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
  }
  TCFuture<int> f_DepthChildCastCleared() {
    int *pEntry = static_cast<CEntry &>(mp_Items[0]).m_Children.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = static_cast<CEntry &>(mp_Items[0]).m_Children.f_FindEqual(0);
      mp_Items[0].m_Children.f_Clear();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_DepthChildCastDataReset() {
    int *pEntry = static_cast<CEntry &>(mp_Items[0]).m_Children.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = static_cast<CEntry &>(mp_Items[0]).m_Children.f_FindEqual(0);
      mp_Items[0].m_pData = TCOwner<CHeapData>{};
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_DepthOwnedCastDataReset() {
    int *pEntry = &static_cast<CEntry &>(mp_Items[0]).m_pData->m_Value;
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = &static_cast<CEntry &>(mp_Items[0]).m_pData->m_Value;
      mp_Items[0].m_pData = TCOwner<CHeapData>{};
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_DepthOwnedCastChildrenCleared() {
    int *pEntry = &static_cast<CEntry &>(mp_Items[0]).m_pData->m_Value;
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = &static_cast<CEntry &>(mp_Items[0]).m_pData->m_Value;
      mp_Items[0].m_Children.f_Clear();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_DepthChildClearedThroughCast() {
    int *pEntry = mp_Items[0].m_Children.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items[0].m_Children.f_FindEqual(0);
      static_cast<CEntry &>(mp_Items[0]).m_Children.f_Clear();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_DepthOwnedResetThroughCast() {
    int *pEntry = &mp_Items[0].m_pData->m_Value;
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = &mp_Items[0].m_pData->m_Value;
      static_cast<CEntry &>(mp_Items[0]).m_pData = TCOwner<CHeapData>{};
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_DepthEntryKeptThroughCast() {
    int *pEntry = &mp_Items[0].m_Value;
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = &mp_Items[0].m_Value;
      static_cast<CEntry &>(mp_Items[0]).m_pData = TCOwner<CHeapData>{};
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
  }
  TCFuture<int> f_DepthEntryIdentityRemoved() {
    int *pEntry = &fg_Same(mp_Items[0]).m_Value;
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = &fg_Same(mp_Items[0]).m_Value;
      mp_Items.f_Remove(0);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_DepthEntryIdentityChildrenCleared() {
    int *pEntry = &fg_Same(mp_Items[0]).m_Value;
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = &fg_Same(mp_Items[0]).m_Value;
      mp_Items[0].m_Children.f_Clear();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
  }
  TCFuture<int> f_DepthEntryIdentityDataReset() {
    int *pEntry = &fg_Same(mp_Items[0]).m_Value;
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = &fg_Same(mp_Items[0]).m_Value;
      mp_Items[0].m_pData = TCOwner<CHeapData>{};
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
  }
  TCFuture<int> f_DepthChildIdentityCleared() {
    int *pEntry = fg_Same(mp_Items[0]).m_Children.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = fg_Same(mp_Items[0]).m_Children.f_FindEqual(0);
      mp_Items[0].m_Children.f_Clear();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_DepthChildIdentityDataReset() {
    int *pEntry = fg_Same(mp_Items[0]).m_Children.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = fg_Same(mp_Items[0]).m_Children.f_FindEqual(0);
      mp_Items[0].m_pData = TCOwner<CHeapData>{};
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_DepthOwnedIdentityDataReset() {
    int *pEntry = &fg_Same(mp_Items[0]).m_pData->m_Value;
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = &fg_Same(mp_Items[0]).m_pData->m_Value;
      mp_Items[0].m_pData = TCOwner<CHeapData>{};
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_DepthOwnedIdentityChildrenCleared() {
    int *pEntry = &fg_Same(mp_Items[0]).m_pData->m_Value;
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = &fg_Same(mp_Items[0]).m_pData->m_Value;
      mp_Items[0].m_Children.f_Clear();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_DepthChildClearedThroughIdentity() {
    int *pEntry = mp_Items[0].m_Children.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items[0].m_Children.f_FindEqual(0);
      fg_Same(mp_Items[0]).m_Children.f_Clear();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_DepthOwnedResetThroughIdentity() {
    int *pEntry = &mp_Items[0].m_pData->m_Value;
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = &mp_Items[0].m_pData->m_Value;
      fg_Same(mp_Items[0]).m_pData = TCOwner<CHeapData>{};
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_DepthEntryKeptThroughIdentity() {
    int *pEntry = &mp_Items[0].m_Value;
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = &mp_Items[0].m_Value;
      fg_Same(mp_Items[0]).m_pData = TCOwner<CHeapData>{};
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
  }
  TCFuture<int> f_DepthEntryConditionalRemoved() {
    int *pEntry = &(m_bChoose ? mp_Items[0] : mp_Items[0]).m_Value;
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = &(m_bChoose ? mp_Items[0] : mp_Items[0]).m_Value;
      mp_Items.f_Remove(0);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_DepthEntryConditionalChildrenCleared() {
    int *pEntry = &(m_bChoose ? mp_Items[0] : mp_Items[0]).m_Value;
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = &(m_bChoose ? mp_Items[0] : mp_Items[0]).m_Value;
      mp_Items[0].m_Children.f_Clear();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
  }
  TCFuture<int> f_DepthEntryConditionalDataReset() {
    int *pEntry = &(m_bChoose ? mp_Items[0] : mp_Items[0]).m_Value;
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = &(m_bChoose ? mp_Items[0] : mp_Items[0]).m_Value;
      mp_Items[0].m_pData = TCOwner<CHeapData>{};
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
  }
  TCFuture<int> f_DepthChildConditionalCleared() {
    int *pEntry = (m_bChoose ? mp_Items[0] : mp_Items[0]).m_Children.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = (m_bChoose ? mp_Items[0] : mp_Items[0]).m_Children.f_FindEqual(0);
      mp_Items[0].m_Children.f_Clear();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_DepthChildConditionalDataReset() {
    int *pEntry = (m_bChoose ? mp_Items[0] : mp_Items[0]).m_Children.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = (m_bChoose ? mp_Items[0] : mp_Items[0]).m_Children.f_FindEqual(0);
      mp_Items[0].m_pData = TCOwner<CHeapData>{};
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_DepthOwnedConditionalDataReset() {
    int *pEntry = &(m_bChoose ? mp_Items[0] : mp_Items[0]).m_pData->m_Value;
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = &(m_bChoose ? mp_Items[0] : mp_Items[0]).m_pData->m_Value;
      mp_Items[0].m_pData = TCOwner<CHeapData>{};
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_DepthOwnedConditionalChildrenCleared() {
    int *pEntry = &(m_bChoose ? mp_Items[0] : mp_Items[0]).m_pData->m_Value;
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = &(m_bChoose ? mp_Items[0] : mp_Items[0]).m_pData->m_Value;
      mp_Items[0].m_Children.f_Clear();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_DepthChildClearedThroughConditional() {
    int *pEntry = mp_Items[0].m_Children.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items[0].m_Children.f_FindEqual(0);
      (m_bChoose ? mp_Items[0] : mp_Items[0]).m_Children.f_Clear();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_DepthOwnedResetThroughConditional() {
    int *pEntry = &mp_Items[0].m_pData->m_Value;
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = &mp_Items[0].m_pData->m_Value;
      (m_bChoose ? mp_Items[0] : mp_Items[0]).m_pData = TCOwner<CHeapData>{};
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_DepthEntryKeptThroughConditional() {
    int *pEntry = &mp_Items[0].m_Value;
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = &mp_Items[0].m_Value;
      (m_bChoose ? mp_Items[0] : mp_Items[0]).m_pData = TCOwner<CHeapData>{};
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
  }
  TCFuture<int> f_DepthEntryCommaRemoved() {
    int *pEntry = &((void)0, mp_Items[0]).m_Value;
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = &((void)0, mp_Items[0]).m_Value;
      mp_Items.f_Remove(0);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_DepthEntryCommaChildrenCleared() {
    int *pEntry = &((void)0, mp_Items[0]).m_Value;
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = &((void)0, mp_Items[0]).m_Value;
      mp_Items[0].m_Children.f_Clear();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
  }
  TCFuture<int> f_DepthEntryCommaDataReset() {
    int *pEntry = &((void)0, mp_Items[0]).m_Value;
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = &((void)0, mp_Items[0]).m_Value;
      mp_Items[0].m_pData = TCOwner<CHeapData>{};
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
  }
  TCFuture<int> f_DepthChildCommaCleared() {
    int *pEntry = ((void)0, mp_Items[0]).m_Children.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = ((void)0, mp_Items[0]).m_Children.f_FindEqual(0);
      mp_Items[0].m_Children.f_Clear();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_DepthChildCommaDataReset() {
    int *pEntry = ((void)0, mp_Items[0]).m_Children.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = ((void)0, mp_Items[0]).m_Children.f_FindEqual(0);
      mp_Items[0].m_pData = TCOwner<CHeapData>{};
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_DepthOwnedCommaDataReset() {
    int *pEntry = &((void)0, mp_Items[0]).m_pData->m_Value;
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = &((void)0, mp_Items[0]).m_pData->m_Value;
      mp_Items[0].m_pData = TCOwner<CHeapData>{};
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_DepthOwnedCommaChildrenCleared() {
    int *pEntry = &((void)0, mp_Items[0]).m_pData->m_Value;
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = &((void)0, mp_Items[0]).m_pData->m_Value;
      mp_Items[0].m_Children.f_Clear();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_DepthChildClearedThroughComma() {
    int *pEntry = mp_Items[0].m_Children.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items[0].m_Children.f_FindEqual(0);
      ((void)0, mp_Items[0]).m_Children.f_Clear();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_DepthOwnedResetThroughComma() {
    int *pEntry = &mp_Items[0].m_pData->m_Value;
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = &mp_Items[0].m_pData->m_Value;
      ((void)0, mp_Items[0]).m_pData = TCOwner<CHeapData>{};
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_DepthEntryKeptThroughComma() {
    int *pEntry = &mp_Items[0].m_Value;
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = &mp_Items[0].m_Value;
      ((void)0, mp_Items[0]).m_pData = TCOwner<CHeapData>{};
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
  }
  TCFuture<int> f_DepthEntryLambdaRemoved() {
    int *pEntry = &[&]() -> CEntry & { return mp_Items[0]; }().m_Value;
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = &[&]() -> CEntry & { return mp_Items[0]; }().m_Value;
      mp_Items.f_Remove(0);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_DepthEntryLambdaChildrenCleared() {
    int *pEntry = &[&]() -> CEntry & { return mp_Items[0]; }().m_Value;
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = &[&]() -> CEntry & { return mp_Items[0]; }().m_Value;
      mp_Items[0].m_Children.f_Clear();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
  }
  TCFuture<int> f_DepthEntryLambdaDataReset() {
    int *pEntry = &[&]() -> CEntry & { return mp_Items[0]; }().m_Value;
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = &[&]() -> CEntry & { return mp_Items[0]; }().m_Value;
      mp_Items[0].m_pData = TCOwner<CHeapData>{};
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
  }
  TCFuture<int> f_DepthChildLambdaCleared() {
    int *pEntry = [&]() -> CEntry & { return mp_Items[0]; }().m_Children.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = [&]() -> CEntry & { return mp_Items[0]; }().m_Children.f_FindEqual(0);
      mp_Items[0].m_Children.f_Clear();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_DepthChildLambdaDataReset() {
    int *pEntry = [&]() -> CEntry & { return mp_Items[0]; }().m_Children.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = [&]() -> CEntry & { return mp_Items[0]; }().m_Children.f_FindEqual(0);
      mp_Items[0].m_pData = TCOwner<CHeapData>{};
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_DepthOwnedLambdaDataReset() {
    int *pEntry = &[&]() -> CEntry & { return mp_Items[0]; }().m_pData->m_Value;
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = &[&]() -> CEntry & { return mp_Items[0]; }().m_pData->m_Value;
      mp_Items[0].m_pData = TCOwner<CHeapData>{};
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_DepthOwnedLambdaChildrenCleared() {
    int *pEntry = &[&]() -> CEntry & { return mp_Items[0]; }().m_pData->m_Value;
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = &[&]() -> CEntry & { return mp_Items[0]; }().m_pData->m_Value;
      mp_Items[0].m_Children.f_Clear();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_DepthChildClearedThroughLambda() {
    int *pEntry = mp_Items[0].m_Children.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items[0].m_Children.f_FindEqual(0);
      [&]() -> CEntry & { return mp_Items[0];
      }().m_Children.f_Clear();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_DepthOwnedResetThroughLambda() {
    int *pEntry = &mp_Items[0].m_pData->m_Value;
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = &mp_Items[0].m_pData->m_Value;
      [&]() -> CEntry & { return mp_Items[0];
      }().m_pData = TCOwner<CHeapData>{};
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_DepthEntryKeptThroughLambda() {
    int *pEntry = &mp_Items[0].m_Value;
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = &mp_Items[0].m_Value;
      [&]() -> CEntry & { return mp_Items[0];
      }().m_pData = TCOwner<CHeapData>{};
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
  }
  TCFuture<int> f_DepthOwnerAccessorReset() {
    int *pEntry = &mp_Items[0].f_Owner()->m_Value;
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = &mp_Items[0].f_Owner()->m_Value;
      mp_Items[0].m_pData = TCOwner<CHeapData>{};
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_DepthOwnerAccessorChildrenCleared() {
    int *pEntry = &mp_Items[0].f_Owner()->m_Value;
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = &mp_Items[0].f_Owner()->m_Value;
      mp_Items[0].m_Children.f_Clear();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_DepthOwnerAliasReset() {
    auto &Owner = mp_Items[0].m_pData;
    int *pEntry = &Owner->m_Value;
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      auto &Owner = mp_Items[0].m_pData;
      pEntry = &Owner->m_Value;
      mp_Items[0].m_pData = TCOwner<CHeapData>{};
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_DepthOwnerAliasChildrenCleared() {
    auto &Owner = mp_Items[0].m_pData;
    int *pEntry = &Owner->m_Value;
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      auto &Owner = mp_Items[0].m_pData;
      pEntry = &Owner->m_Value;
      mp_Items[0].m_Children.f_Clear();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_DepthOwnerAliasInLambdaReset() {
    int *pEntry = &[&]() -> TCOwner<CHeapData> & { auto &Owner = mp_Items[0].m_pData; return Owner; }()->m_Value;
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = &[&]() -> TCOwner<CHeapData> & { auto &Owner = mp_Items[0].m_pData; return Owner; }()->m_Value;
      mp_Items[0].m_pData = TCOwner<CHeapData>{};
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_DepthOwnerAliasInLambdaChildrenCleared() {
    int *pEntry = &[&]() -> TCOwner<CHeapData> & { auto &Owner = mp_Items[0].m_pData; return Owner; }()->m_Value;
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = &[&]() -> TCOwner<CHeapData> & { auto &Owner = mp_Items[0].m_pData; return Owner; }()->m_Value;
      mp_Items[0].m_Children.f_Clear();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_DepthOwnerOfEntryAliasReset() {
    auto &Entry = mp_Items[0];
    int *pEntry = &Entry.m_pData->m_Value;
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      auto &Entry = mp_Items[0];
      pEntry = &Entry.m_pData->m_Value;
      mp_Items[0].m_pData = TCOwner<CHeapData>{};
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_DepthOwnerOfEntryAliasChildrenCleared() {
    auto &Entry = mp_Items[0];
    int *pEntry = &Entry.m_pData->m_Value;
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      auto &Entry = mp_Items[0];
      pEntry = &Entry.m_pData->m_Value;
      mp_Items[0].m_Children.f_Clear();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_DepthOwnerCastReset() {
    int *pEntry = &static_cast<TCOwner<CHeapData> &>(mp_Items[0].m_pData)->m_Value;
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = &static_cast<TCOwner<CHeapData> &>(mp_Items[0].m_pData)->m_Value;
      mp_Items[0].m_pData = TCOwner<CHeapData>{};
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_DepthOwnerCastChildrenCleared() {
    int *pEntry = &static_cast<TCOwner<CHeapData> &>(mp_Items[0].m_pData)->m_Value;
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = &static_cast<TCOwner<CHeapData> &>(mp_Items[0].m_pData)->m_Value;
      mp_Items[0].m_Children.f_Clear();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_DepthOwnerIdentityReset() {
    int *pEntry = &fg_Same(mp_Items[0].m_pData)->m_Value;
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = &fg_Same(mp_Items[0].m_pData)->m_Value;
      mp_Items[0].m_pData = TCOwner<CHeapData>{};
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_DepthOwnerIdentityChildrenCleared() {
    int *pEntry = &fg_Same(mp_Items[0].m_pData)->m_Value;
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = &fg_Same(mp_Items[0].m_pData)->m_Value;
      mp_Items[0].m_Children.f_Clear();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_DepthOwnerDereferenceReset() {
    int *pEntry = &(*mp_Items[0].m_pData).m_Value;
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = &(*mp_Items[0].m_pData).m_Value;
      mp_Items[0].m_pData = TCOwner<CHeapData>{};
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_DepthOwnerDereferenceChildrenCleared() {
    int *pEntry = &(*mp_Items[0].m_pData).m_Value;
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = &(*mp_Items[0].m_pData).m_Value;
      mp_Items[0].m_Children.f_Clear();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_DepthBackingGetterReset() {
    int *pEntry = &mp_Items[0].f_Data()->m_Value;
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = &mp_Items[0].f_Data()->m_Value;
      mp_Items[0].m_pData = TCOwner<CHeapData>{};
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_DepthBackingGetterChildrenCleared() {
    int *pEntry = &mp_Items[0].f_Data()->m_Value;
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = &mp_Items[0].f_Data()->m_Value;
      mp_Items[0].m_Children.f_Clear();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_DepthSubobjectGetterCleared() {
    int *pEntry = mp_Items[0].f_Children().f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items[0].f_Children().f_FindEqual(0);
      mp_Items[0].m_Children.f_Clear();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_DepthSubobjectGetterDataReset() {
    int *pEntry = mp_Items[0].f_Children().f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items[0].f_Children().f_FindEqual(0);
      mp_Items[0].m_pData = TCOwner<CHeapData>{};
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_DepthNestedGetterCleared() {
    int *pEntry = mp_Items[0].f_FirstChild();
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items[0].f_FirstChild();
      mp_Items[0].m_Children.f_Clear();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_DepthNestedGetterDataReset() {
    int *pEntry = mp_Items[0].f_FirstChild();
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items[0].f_FirstChild();
      mp_Items[0].m_pData = TCOwner<CHeapData>{};
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_DepthCollectionGetterCleared() {
    int *pEntry = mp_Items.f_Grandchild();
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_Grandchild();
      mp_Items[0].m_Children.f_Clear();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_DepthCollectionGetterDataReset() {
    int *pEntry = mp_Items.f_Grandchild();
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_Grandchild();
      mp_Items[0].m_pData = TCOwner<CHeapData>{};
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_DepthMemberAliasEntryRemoved() {
    auto &Inline = mp_Items[0].m_Inline;
    int *pEntry = &Inline.m_Value;
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      auto &Inline = mp_Items[0].m_Inline;
      pEntry = &Inline.m_Value;
      mp_Items.f_Remove(0);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_DepthMemberAliasChildrenCleared() {
    auto &Inline = mp_Items[0].m_Inline;
    int *pEntry = &Inline.m_Value;
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      auto &Inline = mp_Items[0].m_Inline;
      pEntry = &Inline.m_Value;
      mp_Items[0].m_Children.f_Clear();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
  }
  TCFuture<int> f_DepthMemberAliasDataReset() {
    auto &Inline = mp_Items[0].m_Inline;
    int *pEntry = &Inline.m_Value;
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      auto &Inline = mp_Items[0].m_Inline;
      pEntry = &Inline.m_Value;
      mp_Items[0].m_pData = TCOwner<CHeapData>{};
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
  }
  TCFuture<int> f_DepthChildrenAliasEntryRemoved() {
    auto &Children = mp_Items[0].m_Children;
    int *pEntry = Children.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      auto &Children = mp_Items[0].m_Children;
      pEntry = Children.f_FindEqual(0);
      mp_Items.f_Remove(0);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_DepthChildrenAliasChildrenCleared() {
    auto &Children = mp_Items[0].m_Children;
    int *pEntry = Children.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      auto &Children = mp_Items[0].m_Children;
      pEntry = Children.f_FindEqual(0);
      mp_Items[0].m_Children.f_Clear();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_DepthChildrenAliasDataReset() {
    auto &Children = mp_Items[0].m_Children;
    int *pEntry = Children.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      auto &Children = mp_Items[0].m_Children;
      pEntry = Children.f_FindEqual(0);
      mp_Items[0].m_pData = TCOwner<CHeapData>{};
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_DepthChildrenOfEntryAliasEntryRemoved() {
    auto &Entry = mp_Items[0];
    auto &Children = Entry.m_Children;
    int *pEntry = Children.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      auto &Entry = mp_Items[0];
      auto &Children = Entry.m_Children;
      pEntry = Children.f_FindEqual(0);
      mp_Items.f_Remove(0);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_DepthChildrenOfEntryAliasChildrenCleared() {
    auto &Entry = mp_Items[0];
    auto &Children = Entry.m_Children;
    int *pEntry = Children.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      auto &Entry = mp_Items[0];
      auto &Children = Entry.m_Children;
      pEntry = Children.f_FindEqual(0);
      mp_Items[0].m_Children.f_Clear();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_DepthEntryOfEntryAliasEntryRemoved() {
    auto &Child = *mp_Items[0].m_Children.f_FindEqual(0);
    int *pEntry = &Child;
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      auto &Child = *mp_Items[0].m_Children.f_FindEqual(0);
      pEntry = &Child;
      mp_Items.f_Remove(0);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_DepthEntryOfEntryAliasChildrenCleared() {
    auto &Child = *mp_Items[0].m_Children.f_FindEqual(0);
    int *pEntry = &Child;
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      auto &Child = *mp_Items[0].m_Children.f_FindEqual(0);
      pEntry = &Child;
      mp_Items[0].m_Children.f_Clear();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_DepthOwnedValueAliasEntryRemoved() {
    auto &Data = *mp_Items[0].m_pData;
    int *pEntry = &Data.m_Value;
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      auto &Data = *mp_Items[0].m_pData;
      pEntry = &Data.m_Value;
      mp_Items.f_Remove(0);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_DepthOwnedValueAliasDataReset() {
    auto &Data = *mp_Items[0].m_pData;
    int *pEntry = &Data.m_Value;
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      auto &Data = *mp_Items[0].m_pData;
      pEntry = &Data.m_Value;
      mp_Items[0].m_pData = TCOwner<CHeapData>{};
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_DepthOwnerEntryReplaced() {
    int *pEntry = &mp_Owners[0]->m_Value;
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = &mp_Owners[0]->m_Value;
      mp_Owners[0] = TCOwner<CHeapData>{};
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_DepthOwnerEntryReplacedThroughAlias() {
    int *pEntry = &mp_Owners[0]->m_Value;
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = &mp_Owners[0]->m_Value;
      auto &Owner = mp_Owners[0];
      Owner = {};
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_DepthOwnerEntryCleared() {
    int *pEntry = &mp_ClearableOwners[0]->m_Value;
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = &mp_ClearableOwners[0]->m_Value;
      mp_ClearableOwners[0].f_Clear();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_DepthOwnerEntryDereferenced() {
    int *pEntry = &(*mp_Owners[0]).m_Value;
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = &(*mp_Owners[0]).m_Value;
      mp_Owners[0] = TCOwner<CHeapData>{};
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }

  // Replacing what holds an owning pointer or a union in an entry.
  TCFuture<int> f_DepthHolderReplaced() {
    int *pEntry = &mp_Holders[0].m_Holder.m_pData->m_Value;
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = &mp_Holders[0].m_Holder.m_pData->m_Value;
      mp_Holders[0].m_Holder = CDataHolder{};
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_DepthHolderEntryReplaced() {
    int *pEntry = &mp_Holders[0].m_Holder.m_pData->m_Value;
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = &mp_Holders[0].m_Holder.m_pData->m_Value;
      mp_Holders[0] = CHolderEntry{};
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_DepthHolderEntryReplacedThroughAlias() {
    int *pEntry = &mp_Holders[0].m_Holder.m_pData->m_Value;
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = &mp_Holders[0].m_Holder.m_pData->m_Value;
      auto &Entry = mp_Holders[0];
      Entry = CHolderEntry{};
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_DepthHolderReplacedEntryKept() {
    int *pEntry = &mp_Holders[0].m_Value;
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = &mp_Holders[0].m_Value;
      mp_Holders[0].m_Holder = CDataHolder{};
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
  }
  TCFuture<int> f_DepthUnionStateReplaced() {
    int *pEntry = &mp_States[0].m_State.m_Int;
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = &mp_States[0].m_State.m_Int;
      mp_States[0].m_State = CUnionState{};
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_DepthUnionStateReplacedEntry() {
    int *pEntry = &mp_States[0].m_Value;
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = &mp_States[0].m_Value;
      mp_States[0].m_State = CUnionState{};
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_DepthUnionStateMoved() {
    int *pEntry = &mp_States[0].m_Value;
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = &mp_States[0].m_Value;
      CStateEntry Taken = fg_Move(mp_States[0]);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
  }
  TCFuture<int> f_DepthUnionStateMovedInMember() {
    int *pEntry = &mp_States[0].m_Value;
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = &mp_States[0].m_Value;
      mp_States[0].f_MoveOut();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
  }
  TCFuture<int> f_DepthOwnerForwardedInHelper() {
    int *pEntry = &mp_Items[0].m_pData->m_Value;
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = &mp_Items[0].m_pData->m_Value;
      fg_ForwardData(mp_Items[0].m_pData);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_DepthOwnerForwardedInHelperEntryKept() {
    int *pEntry = &mp_Items[0].m_Value;
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = &mp_Items[0].m_Value;
      fg_ForwardData(mp_Items[0].m_pData);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleIteratorThroughPointerRemoves() {
    CEntry *pEntry = mp_IteratorItems.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_IteratorItems.f_FindEqual(0);
      mp_IteratorItems.f_GetIteratorPointer()->f_Remove();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleCastMemberFunctionPointer() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      mp_Items.f_TouchCast();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }

  // Self-returning dereference operators, iterators of iterators, what can't
  // be seen given an entry, member template owners, pointer references into
  // unions, references to local pointers and what isn't evaluated.
  TCFuture<int> f_DepthSelfDerefDereferenceCleared() {
    int *pEntry = mp_SelfDerefs[0].m_Children.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_SelfDerefs[0].m_Children.f_FindEqual(0);
      (*mp_SelfDerefs[0].m_Children).f_Clear();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_DepthSelfDerefDereferenceClearedEntryKept() {
    int *pEntry = &mp_SelfDerefs[0].m_Value;
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = &mp_SelfDerefs[0].m_Value;
      (*mp_SelfDerefs[0].m_Children).f_Clear();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
  }
  TCFuture<int> f_DepthSelfDerefArrowCleared() {
    int *pEntry = mp_SelfDerefs[0].m_Children.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_SelfDerefs[0].m_Children.f_FindEqual(0);
      mp_SelfDerefs[0].m_Children->f_Clear();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_DepthSelfDerefArrowClearedEntryKept() {
    int *pEntry = &mp_SelfDerefs[0].m_Value;
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = &mp_SelfDerefs[0].m_Value;
      mp_SelfDerefs[0].m_Children->f_Clear();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
  }
  TCFuture<int> f_DepthSelfDerefGetCleared() {
    int *pEntry = mp_SelfDerefs[0].m_Children.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_SelfDerefs[0].m_Children.f_FindEqual(0);
      mp_SelfDerefs[0].m_Children.get()->f_Clear();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_DepthSelfDerefGetClearedEntryKept() {
    int *pEntry = &mp_SelfDerefs[0].m_Value;
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = &mp_SelfDerefs[0].m_Value;
      mp_SelfDerefs[0].m_Children.get()->f_Clear();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
  }
  TCFuture<int> f_DepthSelfDerefNamedDereferenceCleared() {
    int *pEntry = mp_SelfDerefs[0].m_Children.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_SelfDerefs[0].m_Children.f_FindEqual(0);
      mp_SelfDerefs[0].m_Children.operator*().f_Clear();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_DepthSelfDerefNamedDereferenceClearedEntryKept() {
    int *pEntry = &mp_SelfDerefs[0].m_Value;
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = &mp_SelfDerefs[0].m_Value;
      mp_SelfDerefs[0].m_Children.operator*().f_Clear();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
  }
  TCFuture<int> f_DepthSelfDerefLookupCleared() {
    int *pEntry = (*mp_SelfDerefs[0].m_Children).f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = (*mp_SelfDerefs[0].m_Children).f_FindEqual(0);
      mp_SelfDerefs[0].m_Children.f_Clear();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_DepthSelfArrowLookupCleared() {
    int *pEntry = mp_SelfDerefs[0].m_Children->f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_SelfDerefs[0].m_Children->f_FindEqual(0);
      mp_SelfDerefs[0].m_Children.f_Clear();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleIteratorOfIteratorRemoves() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      mp_Items.f_GetIterator().f_GetIterator().f_Remove();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleIteratorCopyRemoves() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      auto iEntry = mp_Items.f_GetIterator();
      auto iCopy = iEntry.f_GetIterator();
      iCopy.f_Remove();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleIteratorSteppedRemoves() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      (++mp_Items.f_GetIterator()).f_Remove();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleOpaqueDeletesEntry() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      fg_DeleteEntry(mp_Items.f_FindEqual(0));
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleOpaqueDestroysEntry() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      fg_DestroyEntry(mp_Items[0]);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleOpaqueDeletesPointee() {
    int *pEntry = &mp_Items[0].m_pNext->m_Value;
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = &mp_Items[0].m_pNext->m_Value;
      fg_DeleteEntry(mp_Items[0].m_pNext);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleFreesEntry() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      free(mp_Items.f_FindEqual(0));
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleOpaqueSetsNumber() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      fg_SetNumber(mp_Items[0].m_Value);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_RuleTemplateOwnerReplacedInHelper() {
    CEntry *pEntry = mp_TemplateOwning[0].m_pItems->f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_TemplateOwning[0].m_pItems->f_FindEqual(0);
      fg_ResetTemplateOwner(mp_TemplateOwning[0]);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleTemplateOwnerEntryReplaced() {
    CEntry *pEntry = mp_TemplateOwning[0].m_pItems->f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_TemplateOwning[0].m_pItems->f_FindEqual(0);
      mp_TemplateOwning[0] = CTemplateOwningEntry{};
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleUnionPointerReferenceReset() {
    CEntry *pEntry = mp_Union.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Union.f_FindEqual(0);
      fg_ResetSlotPointer(mp_Union.m_pSlot);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleUnionPointerReferenceAliasReset() {
    CEntry *pEntry = mp_Union.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Union.f_FindEqual(0);
      fg_ResetSlotPointerAlias(mp_Union.m_pSlot);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleReferenceToPointerReadAtUse() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      CItems Local;
      CItems *pItems = &Local;
      auto &rpItems = pItems;
      pItems = &mp_Items;
      rpItems->f_Clear();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleReferenceToPointerReadAtUseElsewhere() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      CItems Local;
      CItems *pItems = &mp_Items;
      auto &rpItems = pItems;
      pItems = &Local;
      rpItems->f_Clear();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_RuleConstantProbe() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      mp_Items.f_ConstantProbe();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_RuleObjectSizeProbe() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      mp_Items.f_ObjectSizeProbe();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_RuleClassifyProbe() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      mp_Items.f_ClassifyProbe();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_RuleSizeProbe() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      mp_Items.f_SizeProbe();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_RuleAlignProbe() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      mp_Items.f_AlignProbe();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_RuleNoexceptProbe() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      mp_Items.f_NoexceptProbe();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_RuleTypeidProbe() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      mp_Items.f_TypeidProbe();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_RuleConstantProbeInCallback() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      (void)__builtin_constant_p((mp_Items.f_Clear(), 0));
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_RuleSizeProbeInCallback() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      (void)sizeof(mp_Items.f_Remove(0));
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_RuleClassifyProbeInCallback() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      (void)__builtin_classify_type(mp_Items.f_Remove(0));
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }

  // What runs implicitly in a helper, virtual calls, escapes, self-returning
  // operators without bodies and reassigned iterators.
  TCFuture<int> f_RuleInitializerClears() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      fg_MakeInitClear(mp_Items);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleInitializerClearsNested() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      fg_OuterInitClear(mp_Items);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleBaseInitializerClears() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      fg_MakeBaseClear(mp_Items);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleLocalDestructorClears() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      fg_LocalClearOnExit(mp_Items);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleLocalDestructorClearsNested() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      fg_OuterLocalClearOnExit(mp_Items);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleTemporaryDestructorClears() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      fg_TemporaryClearOnExit(mp_Items);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleMemberDestructorClears() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      fg_MemberClearOnExit(mp_Items);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleOpaqueDestructor() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      fg_OpaqueEnd(mp_Items);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleAllocatorClears() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      fg_Placed(mp_Items);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleExplicitDestructor() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      fg_Destroy(mp_Items);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleExplicitDestructorNested() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      fg_OuterDestroy(mp_Items);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleExplicitDestructorInCallback() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      mp_Items.~CItems();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleCleanupFunctionClears() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      fg_CleanupFunction(mp_Items);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleConstructedInCallback() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      CInitClear Made(mp_Items);
      (void)Made;
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleLocalDestructorInCallback() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      CClearOnExit Cleanup{mp_Items};
      (void)Cleanup;
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleVirtualArgument() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      fg_ApplyVirtual(m_Processor, mp_Items);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleVirtualArgumentQualified() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      fg_ApplyQualified(m_Processor, mp_Items);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_RuleVirtualArgumentFinal() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      fg_ApplyFinal(*(CFinalProcessor *)nullptr, mp_Items);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_RuleVirtualArgumentExact() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      fg_ApplyExact(mp_Items);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_RuleVirtualArgumentInCallback() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      m_Processor.f_Apply(mp_Items);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleVirtualArgumentInCallbackExact() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      mp_ProcessorValue.f_Apply(mp_Items);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_RuleEscapes() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      fg_Save(mp_Items);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleEscapesThenCleared() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      fg_SaveThenClear(mp_Items);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleEscapesStatic() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      fg_KeepStatic(mp_Items);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleKeptLocal() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      fg_KeepLocal(mp_Items);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_RuleLocalPointerRedirectedInHelper() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      CItems *pItems = &mp_Items;
      fg_Redirect(&pItems);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_RuleDrainedThroughMemberPointer() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      fg_DrainThrough(mp_Items);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleReassignedIterator() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      fg_RemoveReassigned(mp_Items, mp_Other);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleSteppedIterator() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      fg_RemoveStepped(mp_Items, mp_Other);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_RuleVirtualRemovingOverride() {
    CVirtualRemoving *pEntry = mp_VirtualRemoving.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_VirtualRemoving.f_FindEqual(0);
      mp_VirtualRemoving.f_FindEqual(1)->f_Clear();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RulePointerReferenceDeleted() {
    int *pEntry = &mp_Items[0].m_pNext->m_Value;
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = &mp_Items[0].m_pNext->m_Value;
      fg_DeleteEntryRef(mp_Items[0].m_pNext);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleSelfIndexedDeclared() {
    CEntry *pEntry = mp_SelfIndexedDeclared.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_SelfIndexedDeclared.f_FindEqual(0);
      mp_SelfIndexedDeclared[0].f_Clear();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_DepthSelfDerefDeclaredCleared() {
    int *pEntry = mp_SelfDerefDeclared[0].m_Children.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_SelfDerefDeclared[0].m_Children.f_FindEqual(0);
      (*mp_SelfDerefDeclared[0].m_Children).f_Clear();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }

  // Guards in each form of making them, by what their destructors change.
  TCFuture<int> f_RuleGuardBracesOwn() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      fg_GuardBracesOwn(mp_Items);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_RuleGuardBracesRef() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      fg_GuardBracesRef(mp_Items);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleGuardBracesUnknown() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      fg_GuardBracesUnknown(mp_Items);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleGuardParensOwn() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      fg_GuardParensOwn(mp_Items);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_RuleGuardParensRef() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      fg_GuardParensRef(mp_Items);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleGuardParensUnknown() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      fg_GuardParensUnknown(mp_Items);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleGuardBaseOwn() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      fg_GuardBaseOwn(mp_Items);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_RuleGuardBaseRef() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      fg_GuardBaseRef(mp_Items);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleGuardBaseUnknown() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      fg_GuardBaseUnknown(mp_Items);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleGuardConstructorOwn() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      fg_GuardConstructorOwn(mp_Items);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_RuleGuardConstructorRef() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      fg_GuardConstructorRef(mp_Items);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleGuardConstructorUnknown() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      fg_GuardConstructorUnknown(mp_Items);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleGuardConstPointerOwn() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      fg_GuardConstPointerOwn(mp_Items);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_RuleGuardConstPointerRef() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      fg_GuardConstPointerRef(mp_Items);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleGuardConstPointerUnknown() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      fg_GuardConstPointerUnknown(mp_Items);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleGuardArrayOwn() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      fg_GuardArrayOwn(mp_Items);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_RuleGuardArrayRef() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      fg_GuardArrayRef(mp_Items);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleGuardArrayUnknown() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      fg_GuardArrayUnknown(mp_Items);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleGuardExtendedOwn() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      fg_GuardExtendedOwn(mp_Items);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_RuleGuardExtendedRef() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      fg_GuardExtendedRef(mp_Items);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleGuardExtendedUnknown() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      fg_GuardExtendedUnknown(mp_Items);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleGuardTemporaryOwn() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      fg_GuardTemporaryOwn(mp_Items);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_RuleGuardTemporaryRef() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      fg_GuardTemporaryRef(mp_Items);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleGuardTemporaryUnknown() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      fg_GuardTemporaryUnknown(mp_Items);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleGuardFactoryOwn() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      fg_GuardFactoryOwn(mp_Items);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_RuleGuardFactoryRef() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      fg_GuardFactoryRef(mp_Items);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleGuardFactoryUnknown() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      fg_GuardFactoryUnknown(mp_Items);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleGuardOperatorFactoryOwn() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      fg_GuardOperatorFactoryOwn(mp_Items);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_RuleGuardOperatorFactoryRef() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      fg_GuardOperatorFactoryRef(mp_Items);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleGuardOperatorFactoryUnknown() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      fg_GuardOperatorFactoryUnknown(mp_Items);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleGuardReceiverOwn() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      fg_GuardReceiverOwn(mp_Items);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_RuleGuardReceiverRef() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      fg_GuardReceiverRef(mp_Items);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleGuardReceiverUnknown() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      fg_GuardReceiverUnknown(mp_Items);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleGuardInCallbackOwn() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      CAggOwn Guard{mp_Items};
      (void)Guard;
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_RuleGuardArrayInCallbackOwn() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      CAggOwn Guards[1] = {{mp_Items}};
      (void)Guards;
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_RuleGuardExtendedInCallbackOwn() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      const CAggOwn &Guard = CAggOwn{mp_Items};
      (void)Guard;
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_RuleGuardInCallbackRef() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      CAggRef Guard{mp_Items};
      (void)Guard;
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleGuardArrayInCallbackRef() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      CAggRef Guards[1] = {{mp_Items}};
      (void)Guards;
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleGuardExtendedInCallbackRef() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      const CAggRef &Guard = CAggRef{mp_Items};
      (void)Guard;
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleGuardInCallbackUnknown() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      CAggUnknown Guard{mp_Items};
      (void)Guard;
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleGuardArrayInCallbackUnknown() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      CAggUnknown Guards[1] = {{mp_Items}};
      (void)Guards;
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleGuardExtendedInCallbackUnknown() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      const CAggUnknown &Guard = CAggUnknown{mp_Items};
      (void)Guard;
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleAliasWrittenThroughParameter() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      fg_OuterSlot(mp_Items);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleGenericLambdaPattern() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      mp_Items.f_GenericTouch();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_RuleConstPointerReference() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      CItems *pItems = &mp_Items;
      fg_ClearConstPointerRef(pItems);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleViewOperator() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      fg_ViewOperator(mp_Items);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleOutOfLineRemovingEntry() {
    COutOfLineRemoving *pEntry = mp_OutOfLine.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_OutOfLine.f_FindEqual(0);
      mp_OutOfLine[0].f_Remove();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleUnknownAccessorResult() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      mp_Items.f_Select(m_bChoose).f_Clear();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleUnknownAccessorResultToHelper() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      fg_ClearItems(mp_Items.f_Select(m_bChoose));
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleUnknownAccessorResultToLookingHelper() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      fg_LookItems(mp_Items.f_Select(m_bChoose));
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_RuleUnknownAccessorResultToUnseenHelper() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      fg_ClearUnseen(mp_Items.f_Select(m_bChoose));
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleLocalPointerByReferenceToUnseen() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      CItems *pItems = &mp_Items;
      fg_RedirectUnseen(pItems);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_RuleVirtualOperatorExactReceiver() {
    CEntry *pEntry = mp_VirtualCall.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_VirtualCall.f_FindEqual(0);
      mp_VirtualCall();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_RuleCleanupForgetsPointer() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      fg_CleanupForget(mp_Items);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_RuleCleanupValueAssigned() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      fg_CleanupReassigned(mp_Other, mp_Items);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleCleanupValueReassigned() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      fg_CleanupReassigned(mp_Items, mp_Other);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleGuardUnionMember() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      fg_UnionGuard(mp_Items);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleGuardAfterBitField() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      fg_BitGuard(mp_Items);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleGuardArrayMember() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      fg_ArrayGuard(mp_Items);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleGuardAssignedLater() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      fg_LaterAssigned(mp_Items);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleGuardSetLater() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      fg_LaterSet(mp_Items);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleGuardAssignedLaterOwnState() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      fg_LaterOwn(mp_Items);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_RuleGuardAssignedLaterInCallback() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      CLaterGuard Guard;
      Guard.m_pItems = &mp_Items;
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleGuardSetLaterInCallback() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      CLaterGuard Guard;
      Guard.f_Set(mp_Items);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleCleanupEarlyExit() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      fg_CleanupEarly(mp_Items, m_bChoose);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleAsmWrite() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      fg_AsmWrite(mp_Items);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleAtomicWrite() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      fg_AtomicWrite(mp_Items);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleReferenceToPointerToConst() {
    CEntry *pEntry = const_cast<CItems *>(mp_pConstItems)->f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = const_cast<CItems *>(mp_pConstItems)->f_FindEqual(0);
      fg_DisposeConst(mp_pConstItems);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleReferenceToPointerToConstForwarded() {
    CEntry *pEntry = const_cast<CItems *>(mp_pConstItems)->f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = const_cast<CItems *>(mp_pConstItems)->f_FindEqual(0);
      fg_ForwardConst(mp_pConstItems);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleReferenceToPointerToConstInHelper() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      fg_TidyConst(mp_Items);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleGuardKeptOnlyDiscarded() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      fg_DiscardedLater(mp_Items);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_RuleGuardKeptOnlyUnevaluated() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      fg_UnevaluatedLater(mp_Items);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_RuleGuardKeptOnlyNever() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      fg_NeverLater(mp_Items);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_RuleGuardKeptOnlyDiscardedInCallback() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      CLaterGuard Guard;
      if constexpr (false) Guard.m_pItems = &mp_Items;
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_RuleGuardWrapperMember() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      fg_WrapperGuard(mp_Items);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleGuardWrapperMemberInCallback() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      CWrapperGuard Guard{{&mp_Items}};
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RulePointerMemberRemovesItself() {
    int *pEntry = &mp_Pointing[0].m_pHeap->m_Value;
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = &mp_Pointing[0].m_pHeap->m_Value;
      mp_Pointing[0].m_pHeap->f_Remove();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleAccessorOfOtherCleared() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      f_OtherItems().f_Clear();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_RuleAccessorOfOwnCleared() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      f_OtherItems().f_Clear();
      mp_Items.f_Clear();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleInheritedConstructorClears() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      fg_InheritedClear(mp_Items);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleInheritedConstructorClearsInCallback() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      CInheritsClearing Clearing(mp_Items);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleInheritedConstructorLooks() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      fg_InheritedLook(mp_Items);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_RuleGuardCleanupMember() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      fg_CleanupGuard(mp_Items);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleGuardCleanupMemberInCallback() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      CCleanupGuard Guard{&mp_Items};
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleGuardCleanupMemberOwnState() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      fg_OwnCleanupGuard(mp_Items);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_RuleChildrenClearedThroughAlias() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      fg_ClearChildrenAlias(mp_Items);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_RuleChildrenClearedDirect() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      fg_ClearChildrenDirect(mp_Items);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_RuleGuardKeptInUncalledLambda() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      CLaterGuard Guard;
      auto fNever = [&] { Guard.m_pItems = &mp_Items;};
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_RuleGuardKeptInDiscardedLambda() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      CLaterGuard Guard;
      [&] { Guard.m_pItems = &mp_Items;};
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_RuleGuardKeptInCalledLambda() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      CLaterGuard Guard;
      auto fSet = [&] { Guard.m_pItems = &mp_Items;};
      fSet();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleEntryRemovesItselfInMember() {
    int *pEntry = &mp_Tidying[0].m_Value;
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = &mp_Tidying[0].m_Value;
      mp_Tidying[0].f_Tidy();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleEntryRemovesItselfDirect() {
    int *pEntry = &mp_Tidying[0].m_Value;
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = &mp_Tidying[0].m_Value;
      mp_Tidying[0].f_Remove();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleEntryRemovedByHelper() {
    int *pEntry = &mp_Tidying[0].m_Value;
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = &mp_Tidying[0].m_Value;
      fg_Erase(mp_Tidying[0]);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleEntryRemovedByHelperPointer() {
    int *pEntry = &mp_Tidying[0].m_Value;
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = &mp_Tidying[0].m_Value;
      fg_ErasePointer(&mp_Tidying[0]);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleMadeFromNumber() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      CText Text(mp_Items.f_CountConst());
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_RuleIndependentValue() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      auto Description = mp_Items.f_Description();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_RuleLocalArrayInHelper() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      (void)fg_Batch(mp_Items);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_RuleConstructorKeepsInLocal() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      CObserver Observer(mp_Items);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_RuleConstructorKeepsInLocalInHelper() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      fg_Observe(mp_Items);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_RuleValueWrapperCleared() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      fg_ClearView(mp_Items);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleInlineChildClearedCast() {
    int *pEntry = &mp_CastTidy[0].m_Value;
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = &mp_CastTidy[0].m_Value;
      mp_CastTidy[0].f_TidyCast();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
  }
  TCFuture<int> f_RuleInlineChildClearedSubscript() {
    int *pEntry = &mp_CastTidy[0].m_Value;
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = &mp_CastTidy[0].m_Value;
      mp_CastTidy[0].f_TidySubscript();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
  }
  TCFuture<int> f_RuleInlineChildClearedDirect() {
    int *pEntry = &mp_CastTidy[0].m_Value;
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = &mp_CastTidy[0].m_Value;
      mp_CastTidy[0].f_TidyDirect();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return *pEntry;
  }
  TCFuture<int> f_RuleBackPointerClearsOwner() {
    CBackEntry *pEntry = mp_BackItems.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_BackItems.f_FindEqual(0);
      mp_BackItems[0].m_Children.f_Drop();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleBackPointerClearsOwnerByName() {
    CBackEntry *pEntry = mp_BackItems.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_BackItems.f_FindEqual(0);
      mp_BackItems[0].m_Children.f_Clear();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleBackPointerClearsHolder() {
    CBackOwnerEntry *pEntry = mp_BackOwner.m_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_BackOwner.m_Items.f_FindEqual(0);
      mp_BackOwner.m_Items[0].m_Children.f_Clear();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleOwnBuffer() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      auto Printed = mp_Items.f_Print();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_RuleOwnCollection() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      auto Values = mp_Items.f_Values();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_RuleSetterThenCleared() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      fg_ClearThroughSetter(mp_Items);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleSetterThenLooked() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      fg_LookThroughSetter(mp_Items);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_RuleStoredThroughAlias() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      fg_StoreThroughAlias(mp_Items);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_RuleStoredThroughPointerAlias() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      fg_StoreThroughPointerAlias(mp_Items);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_RuleViewThroughAlias() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      auto View = mp_Items.f_ViewAlias();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleViewThroughPointerAlias() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      auto View = mp_Items.f_ViewPointerAlias();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleViewThroughLocal() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      auto View = mp_Items.f_ViewLocal();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleViewThroughCopy() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      auto View = mp_Items.f_ViewCopy();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleViewOfNothing() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      auto View = mp_Items.f_ViewOwn();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_RuleOwnCopyNotConst() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      TCValues<int> Copy(mp_Counts);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_RuleLocalViewCleared() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      fg_ClearLocalView(mp_Items);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleLocalViewCopyCleared() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      fg_ClearLocalViewCopy(mp_Items);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleReferenceWrapperCleared() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      fg_ClearThroughRef(mp_Items);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleArrayOfPointersCleared() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      fg_ForwardClearBatch(mp_Items);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleGuardStoredThroughAlias() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      fg_GuardThroughAlias(mp_Items);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleGuardStoredThroughPointer() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      fg_GuardThroughPointer(mp_Items);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleGuardSetThroughPointer() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      fg_GuardSetThroughPointer(mp_Items);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleGuardGivenBySetter() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      fg_GuardSetter(mp_Items);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleGuardStoredThroughAliasInCallback() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      CLaterGuard Guard;
      auto &Alias = Guard;
      Alias.m_pItems = &mp_Items;
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleSetterThroughAliasCleared() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      fg_ClearThroughSetterAlias(mp_Items);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleSetterNeverRuns() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      fg_ClearNeverSet(mp_Items, mp_Other);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_RuleLocalPointerGivenToSetter() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      CItems Local;
      CItems *pItems = &mp_Items;
      fg_SetItems(pItems, Local);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_RuleRestoresEntryData() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      CRestore Restore{mp_Items[0].m_Value, 0};
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_CallbackLocalFromCachedPointer() {
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      auto *pCached = mp_Links[0].m_pCached;
      if (!pCached->m_Value)
// CHECK-MESSAGES: :[[@LINE-1]]:12: warning: 'pCached' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return 0;
  }
  TCFuture<int> f_RuleMixedCopyCleared() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      fg_ClearMixedCopy(mp_Items);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleCancellableGuard() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      fg_UseCancellable(mp_Items);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleDeepChain() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      fg_ChainTidy(mp_Items);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleActorGivenClearsOther() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      fg_ClearOther(this);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_RuleActorGivenClearsOwn() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      fg_ClearOwn(this);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleSharedCopyCleared() {
    CEntry *pEntry = mp_SharedEntries[0].m_pChildren->f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_SharedEntries[0].m_pChildren->f_FindEqual(0);
      auto pChildren = mp_SharedEntries[0].m_pChildren;
      pChildren->f_Clear();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleSharedCopyAliasCleared() {
    CEntry *pEntry = mp_SharedEntries[0].m_pChildren->f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_SharedEntries[0].m_pChildren->f_FindEqual(0);
      auto pChildren = mp_SharedEntries[0].m_pChildren;
      auto &rpChildren = pChildren;
      rpChildren->f_Clear();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleSharedCopyLooked() {
    CEntry *pEntry = mp_SharedEntries[0].m_pChildren->f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_SharedEntries[0].m_pChildren->f_FindEqual(0);
      auto pChildren = mp_SharedEntries[0].m_pChildren;
      (void)pChildren->f_FindEqual(1);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_RuleEntryPartAssignedByOperator() {
    CRegisteredEntry *pEntry = mp_Registered.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Registered.f_FindEqual(0);
      mp_Registered[1].m_Registration = CRegistration{};
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleEntryPlainDataAssigned() {
    CRegisteredEntry *pEntry = mp_Registered.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Registered.f_FindEqual(0);
      mp_Registered[1].m_Value = 1;
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_RuleViewGivenByValue() {
    CEntry *pEntry = mp_View.f_Get().f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_View.f_Get().f_FindEqual(0);
      fg_TidyView(mp_View);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_SharedCaptureCleared() {
    CEntry *pEntry = mp_SharedEntries[0].m_pChildren->f_FindEqual(0);
    auto pChildren = mp_SharedEntries[0].m_pChildren;
    auto OnResume = co_await fg_OnResume([this, &pEntry, pChildren]() -> CExceptionPointer {
      pEntry = mp_SharedEntries[0].m_pChildren->f_FindEqual(0);
      pChildren->f_Clear();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_CallbackReferenceToCachedPointer() {
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      auto *const &pSaved = mp_Links[0].m_pCached;
      if (!pSaved->m_Value)
// CHECK-MESSAGES: :[[@LINE-1]]:12: warning: 'pSaved' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return 0;
  }
  TCFuture<int> f_RulePointersCopyCleared() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      auto Copy = mp_EntryPointers;
      Copy.f_Clear();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_RulePointersCopyAliasCleared() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      auto Copy = mp_EntryPointers;
      auto &rCopy = Copy;
      rCopy.f_Clear();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_RuleLocalPointerMemberWritten() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      CItemsView View{&mp_Items};
      View.m_pItems = nullptr;
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_RuleMixedEntryCopyPointerCleared() {
    CEntry *pEntry = mp_MixedEntries[0].m_pOther->f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_MixedEntries[0].m_pOther->f_FindEqual(0);
      auto Copy = mp_MixedEntries[0];
      Copy.m_pOther->f_Clear();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleMixedEntryCopyAliasPointerCleared() {
    CEntry *pEntry = mp_MixedEntries[0].m_pOther->f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_MixedEntries[0].m_pOther->f_FindEqual(0);
      auto Copy = mp_MixedEntries[0];
      auto &rCopy = Copy;
      rCopy.m_pOther->f_Clear();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleMixedEntryCopyChildrenCleared() {
    CEntry *pEntry = mp_MixedEntries[0].m_pOther->f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_MixedEntries[0].m_pOther->f_FindEqual(0);
      auto Copy = mp_MixedEntries[0];
      Copy.m_Children.f_Clear();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_RuleOptionalCopyCleared() {
    CEntry *pEntry = mp_OptionalItems->f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_OptionalItems->f_FindEqual(0);
      auto Copy = mp_OptionalItems;
      Copy->f_Clear();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_RuleOptionalCopyAliasCleared() {
    CEntry *pEntry = mp_OptionalItems->f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_OptionalItems->f_FindEqual(0);
      auto Copy = mp_OptionalItems;
      auto &rCopy = Copy;
      rCopy->f_Clear();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_RuleOptionalCleared() {
    CEntry *pEntry = mp_OptionalItems->f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_OptionalItems->f_FindEqual(0);
      mp_OptionalItems->f_Clear();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleSharedCopyAssigned() {
    CEntry *pEntry = mp_SharedEntries[0].m_pChildren->f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_SharedEntries[0].m_pChildren->f_FindEqual(0);
      auto pChildren = mp_SharedEntries[0].m_pChildren;
      *pChildren = CItems{};
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleDefaultedAssignmentOfEntry() {
    CRegisteredEntry *pEntry = mp_Registered.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Registered.f_FindEqual(0);
      mp_Registered[1] = CRegisteredEntry{};
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  // A callback's copy of an entry: what it holds is its own, what its
  // pointers point to isn't.
  TCFuture<int> f_CallbackCopyOwn() {
    auto &Entry = mp_Peers[0];
    auto OnResume = co_await fg_OnResume([Entry]() -> CExceptionPointer {
      if (Entry.m_Value == 0)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return 0;
  }
  TCFuture<int> f_CallbackCopyBorrowed() {
    auto &Entry = mp_Peers[0];
    auto OnResume = co_await fg_OnResume([Entry]() -> CExceptionPointer {
      if (*Entry.m_pPeer == 0)
// CHECK-MESSAGES: :[[@LINE-1]]:12: warning: 'Entry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return 0;
  }
  TCFuture<int> f_RuleScratchViewCleared() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      mp_Items.f_ScratchView().f_Clear();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleBorrowingTemplateCleared() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      mp_Items.f_BorrowView().f_Clear();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  // What a copy of a shared owner points to isn't kept alive after the
  // copy goes.
  TCFuture<int> f_SharedCopyGone() {
    CEntry *pEntry;
    {
      auto pKeep = mp_SharedEntries[0].m_pChildren;
      pEntry = pKeep->f_FindEqual(0);
    }
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleExplicitObjectAssignment() {
    CExplicitEntry *pEntry = mp_Explicit.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Explicit.f_FindEqual(0);
      mp_Explicit[1].m_Registration = CExplicitRegistration{};
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  // What is in a copy of a collection of pointers, or of an entry, is the
  // copy's own.
  TCFuture<int> f_PointersCopySlot() {
    auto Copy = mp_EntryPointers;
    CEntry *const *pSlot = &Copy[0];
    co_await fg_Work();
    co_return *pSlot != nullptr;
  }
  TCFuture<int> f_MixedEntryCopyChild() {
    auto Copy = mp_MixedEntries[0];
    int *pChild = &Copy.m_Children[0];
    co_await fg_Work();
    co_return *pChild;
  }
  // What is looked up through a copy of a shared owner can be erased, and
  // so can what a closure that copies one gives.
  TCFuture<int> f_SharedCopyLookup() {
    auto pChildren = mp_SharedEntries[0].m_pChildren;
    CEntry *pEntry = pChildren->f_FindEqual(0);
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_SharedCaptureLookup() {
    auto &rpChildren = mp_SharedEntries[0].m_pChildren;
    CEntry *pEntry = [rpChildren] { return rpChildren->f_FindEqual(0); }();
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  // A copy a callback captures of a shared owner a local reference refers to
  // is used as a copy.
  TCFuture<int> f_SharedReferenceCaptureCleared() {
    CEntry *pEntry = mp_SharedEntries[0].m_pChildren->f_FindEqual(0);
    auto &rpChildren = mp_SharedEntries[0].m_pChildren;
    auto OnResume = co_await fg_OnResume([this, &pEntry, rpChildren]() -> CExceptionPointer {
      pEntry = mp_SharedEntries[0].m_pChildren->f_FindEqual(0);
      rpChildren->f_Clear();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  // What a reference member of what is looked up refers to isn't looked up
  // again.
  TCFuture<int> f_CallbackReferenceMember() {
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      auto &Value = mp_RefEntries[0].m_rValue;
      int Copy = Value;
// CHECK-MESSAGES: :[[@LINE-1]]:18: warning: 'Value' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
      if (Copy == 0)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return 0;
  }
  TCFuture<int> f_UsingVirtual() {
    CSelectNode &Selected = mp_Selecting.f_Selected();
    co_await fg_Work();
    co_return Selected.m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'Selected' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_UsingVirtualOperator() {
    CSelectNode &Selected = mp_SelectRoot[0];
    co_await fg_Work();
    co_return Selected.m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'Selected' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_FinalOwnVirtual() {
    CSelectNode &Selected = mp_SelectOwn.f_Selected();
    co_await fg_Work();
    co_return Selected.m_Value;
  }

  // Each kind of collection, its lookups and removals seen or not, through
  // what can change an entry or the collection.
  TCFuture<int> f_RuleOpaqueUnseenDeletesEntry() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      fg_DeleteEntry(mp_Items.f_FindEqual(0));
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleOpaqueUnseenDestroysEntry() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      fg_DestroyEntry(mp_Items[0]);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleOpaqueUnseenDeletesEntryThroughAlias() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      CEntry *pOther = mp_Items.f_FindEqual(0);
      fg_DeleteEntry(pOther);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleOpaqueUnseenReadsEntry() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      fg_TouchEntryOpaque(*mp_Items.f_FindEqual(0));
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_RuleOpaqueRemoves() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      mp_Items.f_Remove(0);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleOpaqueClears() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      mp_Items.f_Clear();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleOpaqueHelperClears() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      fg_ClearItems(mp_Items);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleOpaqueHelperLooks() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      fg_LookItems(mp_Items);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_RuleOpaqueLooks() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(0);
      (void)mp_Items.f_FindEqual(1);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_RuleVisibleUnseenDeletesEntry() {
    CEntry *pEntry = mp_Visible.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Visible.f_FindEqual(0);
      fg_DeleteEntry(mp_Visible.f_FindEqual(0));
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleVisibleUnseenDestroysEntry() {
    CEntry *pEntry = mp_Visible.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Visible.f_FindEqual(0);
      fg_DestroyEntry(mp_Visible[0]);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleVisibleUnseenDeletesEntryThroughAlias() {
    CEntry *pEntry = mp_Visible.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Visible.f_FindEqual(0);
      CEntry *pOther = mp_Visible.f_FindEqual(0);
      fg_DeleteEntry(pOther);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleVisibleUnseenReadsEntry() {
    CEntry *pEntry = mp_Visible.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Visible.f_FindEqual(0);
      fg_TouchEntryOpaque(*mp_Visible.f_FindEqual(0));
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_RuleVisibleRemoves() {
    CEntry *pEntry = mp_Visible.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Visible.f_FindEqual(0);
      mp_Visible.f_Remove(0);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleVisibleClears() {
    CEntry *pEntry = mp_Visible.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Visible.f_FindEqual(0);
      mp_Visible.f_Clear();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleVisibleHelperClears() {
    CEntry *pEntry = mp_Visible.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Visible.f_FindEqual(0);
      fg_ClearVisible(mp_Visible);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleVisibleHelperLooks() {
    CEntry *pEntry = mp_Visible.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Visible.f_FindEqual(0);
      fg_LookVisible(mp_Visible);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_RuleVisibleLooks() {
    CEntry *pEntry = mp_Visible.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Visible.f_FindEqual(0);
      (void)mp_Visible.f_FindEqual(1);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_RuleTreeUnseenDeletesEntry() {
    CEntry *pEntry = mp_Tree.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Tree.f_FindEqual(0);
      fg_DeleteEntry(mp_Tree.f_FindEqual(0));
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleTreeUnseenDestroysEntry() {
    CEntry *pEntry = mp_Tree.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Tree.f_FindEqual(0);
      fg_DestroyEntry(mp_Tree[0]);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleTreeUnseenDeletesEntryThroughAlias() {
    CEntry *pEntry = mp_Tree.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Tree.f_FindEqual(0);
      CEntry *pOther = mp_Tree.f_FindEqual(0);
      fg_DeleteEntry(pOther);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleTreeUnseenReadsEntry() {
    CEntry *pEntry = mp_Tree.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Tree.f_FindEqual(0);
      fg_TouchEntryOpaque(*mp_Tree.f_FindEqual(0));
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_RuleTreeRemoves() {
    CEntry *pEntry = mp_Tree.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Tree.f_FindEqual(0);
      mp_Tree.f_Remove(0);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleTreeClears() {
    CEntry *pEntry = mp_Tree.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Tree.f_FindEqual(0);
      mp_Tree.f_Clear();
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleTreeHelperClears() {
    CEntry *pEntry = mp_Tree.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Tree.f_FindEqual(0);
      fg_ClearTree(mp_Tree);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_RuleTreeHelperLooks() {
    CEntry *pEntry = mp_Tree.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Tree.f_FindEqual(0);
      fg_LookTree(mp_Tree);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_RuleTreeLooks() {
    CEntry *pEntry = mp_Tree.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Tree.f_FindEqual(0);
      (void)mp_Tree.f_FindEqual(1);
      if (!pEntry)
        return fg_MakeError("Gone");
      return nullptr;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_UnevaluatedSizeUse() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    co_await fg_Work();
    co_return int(sizeof(pEntry->m_Value));
  }
  TCFuture<int> f_UnevaluatedConstantUse() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    co_await fg_Work();
    co_return int(__builtin_constant_p(pEntry->m_Value));
  }
  TCFuture<int> f_UnevaluatedNoexceptUse() {
    CEntry *pEntry = mp_Items.f_FindEqual(0);
    co_await fg_Work();
    co_return int(noexcept(pEntry->m_Value));
  }

  // A fixed-size array held by value, in an entry or through a pointer.
  TCFuture<int> f_FixedArrayHeldData() {
    int *pValue = mp_Fixed.data();
    co_await fg_Work();
    co_return *pValue;
  }
  TCFuture<int> f_FixedArrayHeldSubscript() {
    int *pValue = &mp_Fixed[0];
    co_await fg_Work();
    co_return *pValue;
  }
  TCFuture<int> f_FixedArrayHeldBegin() {
    int *pValue = std::begin(mp_Fixed);
    co_await fg_Work();
    co_return *pValue;
  }
  TCFuture<int> f_FixedArrayHeldLoop() {
    int Sum = 0;
    for (int &Value : mp_Fixed) {
      co_await fg_Work();
      Sum += Value;
    }
    co_return Sum;
  }
  TCFuture<int> f_FixedArrayInEntryData() {
    int *pValue = mp_Items[0].m_Fixed.data();
    co_await fg_Work();
    co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pValue' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_FixedArrayInEntrySubscript() {
    int *pValue = &mp_Items[0].m_Fixed[0];
    co_await fg_Work();
    co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pValue' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_FixedArrayInEntryBegin() {
    int *pValue = std::begin(mp_Items[0].m_Fixed);
    co_await fg_Work();
    co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pValue' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_FixedArrayInEntryLoop() {
    int Sum = 0;
    for (int &Value : mp_Items[0].m_Fixed) {
// CHECK-MESSAGES: :[[@LINE-1]]:23: warning: the loop goes on over the data of the actor after a suspension, during which other calls of the actor can change or erase the data; loop over a copy, or look the entry up again after the suspension [malterlib-coroutine-actor-reference]
      co_await fg_Work();
      Sum += Value;
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: 'Value' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
    }
    co_return Sum;
  }
  TCFuture<int> f_FixedArrayThroughPointerData() {
    int *pValue = (*m_pValues).data();
    co_await fg_Work();
    co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pValue' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_FixedArrayThroughPointerSubscript() {
    int *pValue = &(*m_pValues)[0];
    co_await fg_Work();
    co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pValue' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_FixedArrayThroughPointerBegin() {
    int *pValue = std::begin((*m_pValues));
    co_await fg_Work();
    co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pValue' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_FixedArrayThroughPointerLoop() {
    int Sum = 0;
    for (int &Value : (*m_pValues)) {
// CHECK-MESSAGES: :[[@LINE-1]]:23: warning: the loop goes on over the data of the actor after a suspension, during which other calls of the actor can change or erase the data; loop over a copy, or look the entry up again after the suspension [malterlib-coroutine-actor-reference]
      co_await fg_Work();
      Sum += Value;
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: 'Value' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
    }
    co_return Sum;
  }

  TCFuture<int> f_SelfAccessorOperator() {
    CItems &Items = mp_Items();
    co_await fg_Work();
    co_return Items.f_FindEqual(0)->m_Value;
  }
  TCFuture<int> f_SelfAccessorExplicit() {
    CItems &Items = mp_Items.f_ExplicitSelf();
    co_await fg_Work();
    co_return Items.f_FindEqual(0)->m_Value;
  }
  TCFuture<int> f_SelfAccessorExplicitDeclared() {
    CItems &Items = mp_Items.f_ExplicitSelfDeclared();
    co_await fg_Work();
    co_return Items.f_FindEqual(0)->m_Value;
  }

  TCFuture<int> f_FixedArrayThroughPointer() {
    int *pValue = m_pValues->data();
    co_await fg_Work();
    co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pValue' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
};
}
