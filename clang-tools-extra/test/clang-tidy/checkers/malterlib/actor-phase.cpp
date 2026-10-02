// RUN: %check_clang_tidy -std=c++20-or-later %s malterlib-actor-phase %t -- -config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" -- -I %S/Inputs

#include "malterlib-actor-phase.h"

struct CDownload
{
	int m_Value = 0;
};

struct CApp : CDistributedAppActor
{
	DMibActorPhase(Destroy, StopApp) TCFuture<int> fp_DestroyAll()
	{
		mp_Downloads.f_Clear();
		mp_Cache.f_Clear();
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: 'mp_Cache' is declared to lose entries only in Destroy, but is changed here in 'fp_DestroyAll'; change it in a function of that phase (DMibActorPhase) [malterlib-actor-phase]
		co_return 0;
	}

	// An override has the phase of the function it overrides, and calls the
	// function of its base in it.
	TCFuture<int> fp_Destroy() override
	{
		co_await fp_DestroyAll();
		co_await CDistributedAppActor::fp_Destroy();
		for (auto iEntry = mp_Cache.f_GetIterator(); iEntry; )
			iEntry.f_Remove();
		mp_Cache.f_Clear();
		auto fLater = [this]
			{
				mp_Downloads.f_Clear();
			}
		;
		fLater();
		co_return 0;
	}

	TCFuture<int> fp_StopApp() override
	{
		co_await fp_DestroyAll();
		co_return 0;
	}

	TCFuture<int> f_Other()
	{
		co_await fp_DestroyAll();
// CHECK-MESSAGES: :[[@LINE-1]]:12: warning: 'fp_DestroyAll' runs only in the Destroy and StopApp phases of the actor, but is used here in 'f_Other', which can run outside of them; {{.*}}
		co_await fp_Destroy();
// CHECK-MESSAGES: :[[@LINE-1]]:12: warning: 'fp_Destroy' runs only in the Destroy phase of the actor, {{.*}}
		auto pMember = &CApp::fp_DestroyAll;
// CHECK-MESSAGES: :[[@LINE-1]]:19: warning: 'fp_DestroyAll' runs only in the Destroy and StopApp phases of its actor, but a pointer to it is taken here, {{.*}}
		(void)pMember;
		mp_Downloads.f_Remove("Key");
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: 'mp_Downloads' is declared to lose entries only in Destroy, StopApp and Owner, but is changed here in 'f_Other'; change it in a function of those phases (DMibActorPhase), or remove only the entry this call added, by its fresh key [malterlib-actor-phase]
		for (auto iEntry = mp_Cache.f_GetIterator(); iEntry; )
			iEntry.f_Remove();
// CHECK-MESSAGES: :[[@LINE-1]]:4: warning: 'mp_Cache' is declared to lose entries only in Destroy, {{.*}}
		auto &Cache = mp_Cache;
		Cache.f_Clear();
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: 'mp_Cache' is declared to lose entries only in Destroy, {{.*}}
		auto Moved = fg_Move(mp_Cache);
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'mp_Cache' is declared to lose entries only in Destroy, {{.*}}
		mp_Cache = {};
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: 'mp_Cache' is declared to lose entries only in Destroy, {{.*}}
		// Adding, finding and reading entries doesn't remove any, and neither does
		// making a fresh key of the collection, or looping over it.
		mp_Cache["Key"].m_Value = 1;
		auto *pEntry = mp_Cache.f_FindEqual("Key");
		auto const &Constant = mp_Cache;
		auto ID = fg_RandomID(mp_Cache);
		for (auto &Entry : mp_Cache)
			Entry.m_Value = 2;
		(void)pEntry;
		(void)Constant;
		(void)ID;
		co_return 0;
	}

	// The call that added an entry with a fresh key may remove it by that key,
	// also later in a lambda that copies the key.
	TCFuture<int> f_Owner()
	{
		auto DownloadID = fg_RandomID(mp_Downloads);
		auto &Download = mp_Downloads[DownloadID];
		Download.m_Value = 1;
		auto fCleanup = [this, DownloadID]
			{
				mp_Downloads.f_Remove(DownloadID);
			}
		;
		fCleanup();
		mp_Downloads.f_Remove(DownloadID);
		CStr Other = "Other";
		mp_Downloads[Other];
		mp_Downloads.f_Remove(Other);
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: 'mp_Downloads' is declared to lose entries only in Destroy, StopApp and Owner, {{.*}}
		mp_Cache[DownloadID];
		mp_Cache.f_Remove(DownloadID);
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: 'mp_Cache' is declared to lose entries only in Destroy, {{.*}}
		co_return 0;
	}

	// A fresh key passed to the collection by a forwarding reference, and a loop
	// over a collection through free begin and end, don't change them.
	TCFuture<int> f_ForwardingKey()
	{
		auto DownloadID = fg_RandomID(mp_Downloads);
		mp_Downloads[DownloadID];
		mp_Downloads.f_RemoveKey(DownloadID);
		int Sum = 0;
		for (auto &Entry : mp_Free)
			Sum += Entry.m_Value;
		co_return Sum;
	}

	// The phase flags are only ever set.
	void f_Flags()
	{
		mp_State.m_bStoppingApp = true;
		mp_State.m_bStoppingApp = false;
// CHECK-MESSAGES: :[[@LINE-1]]:27: warning: 'm_bStoppingApp' is the flag of a phase, which is only ever set to true, {{.*}}
		bool &bFlag = mp_State.m_bStoppingApp;
// CHECK-MESSAGES: :[[@LINE-1]]:17: warning: 'm_bStoppingApp' is the flag of a phase, {{.*}}
		(void)bFlag;
		if (mp_State.m_bStoppingApp)
			return;
	}

	CApp()
	{
		mp_Cache.f_Clear();
	}

	~CApp()
	{
		mp_Downloads.f_Clear();
	}

	DMibActorEntries(Destroy, StopApp, Owner) TCMap<CStr, CDownload> mp_Downloads;
	DMibActorEntries(Destroy) TCMap<CStr, CDownload> mp_Cache;
	DMibActorEntries() TCFreeList<CDownload> mp_Free;
};

// A phase of another object isn't the phase of this one.
struct COtherApp : CDistributedAppActor
{
	TCFuture<int> fp_Destroy() override
	{
		co_await m_pApp->fp_DestroyAll();
// CHECK-MESSAGES: :[[@LINE-1]]:20: warning: 'fp_DestroyAll' runs only in the Destroy and StopApp phases of its actor, but is used here on another object, {{.*}}
		m_pApp->mp_Cache.f_Clear();
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: 'mp_Cache' is declared to lose entries only in Destroy, {{.*}}
		co_return 0;
	}

	CApp *m_pApp = nullptr;
};

// A defaulted copy assigns the flag with the rest of the object.
struct CFlagState
{
	CFlagState &operator = (CFlagState const &) = default;

	DMibActorPhaseFlag(StopApp) bool m_bStoppingApp = false;
};

// A helper template is checked in its instantiations, an alias keeps the
// object it was made from, and a pointer to the collection is an alias too.
template <typename t_CApp>
void fg_ClearCache(t_CApp &_App)
{
	_App.mp_Cache.f_Clear();
// CHECK-MESSAGES: :[[@LINE-1]]:2: warning: 'mp_Cache' is declared to lose entries only in Destroy, but is changed here in 'fg_ClearCache'; {{.*}}
}

void fg_Instantiate(CApp &_App)
{
	fg_ClearCache(_App);
}

struct CAliasApp : CDistributedAppActor
{
	DMibActorPhase(Destroy) void f_ClearOther(CAliasApp &_Other)
	{
		auto &Alias = _Other.mp_Items;
		Alias.f_Clear();
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: 'mp_Items' is declared to lose entries only in Destroy, but is changed here in 'f_ClearOther'; {{.*}}
		auto &OwnAlias = mp_Items;
		OwnAlias.f_Clear();
	}

	void f_Pointer()
	{
		auto *pItems = &mp_Items;
		pItems->f_Clear();
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: 'mp_Items' is declared to lose entries only in Destroy, but is changed here in 'f_Pointer'; {{.*}}
	}

	// The call that added the entry through an alias removes it.
	void f_OwnerAlias()
	{
		auto &Alias = mp_Owned;
		auto Key = fg_RandomID(Alias);
		Alias[Key];
		Alias.f_Remove(Key);
	}

	DMibActorEntries(Destroy) TCMap<CStr, CDownload> mp_Items;
	DMibActorEntries(Owner) TCMap<CStr, CDownload> mp_Owned;
};

// Keys, objects, iterators and flags that can't be told apart from the own
// ones, and what escapes.
struct CRound22 : CDistributedAppActor
{
	DMibActorPhase(Destroy) int f_DestroyOnly();
	int m_Value = f_DestroyOnly();
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'f_DestroyOnly' runs only in the Destroy phase of the actor, but is used here outside of a function, {{.*}}

	void f_Keys(CStr _Existing, CRound22 *_pOther)
	{
		auto Key = fg_RandomID(mp_Owned);
		mp_Owned[Key];
		Key = _Existing;
		mp_Owned.f_Remove(Key);
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: 'mp_Owned' is declared to lose entries only by the calls that added them, {{.*}}
		auto Fresh = fg_RandomID(mp_Owned);
		mp_Owned[Fresh];
		_pOther->mp_Owned.f_Remove(Fresh);
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: 'mp_Owned' is declared to lose entries only by the calls that added them, {{.*}}
		auto Copy = Fresh;
		mp_Owned.f_Remove(Copy);
	}

	void f_Iterators()
	{
		auto iEntry = mp_Items.f_GetIterator();
		auto iCopy = iEntry;
		iCopy.f_Remove();
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: 'mp_Items' is declared to lose entries only in Destroy, {{.*}}
		mp_Items.f_GetIterator().f_Remove();
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: 'mp_Items' is declared to lose entries only in Destroy, {{.*}}
	}

	TCMap<CStr, CDownload> &f_Expose()
	{
		return mp_Items;
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: 'mp_Items' is declared to lose entries only in Destroy, {{.*}}
	}

	struct CResetFlag
	{
		CResetFlag(bool &o_bFlag);
	};

	void f_Flags()
	{
		CResetFlag Reset(mp_State.m_bStoppingApp);
// CHECK-MESSAGES: :[[@LINE-1]]:20: warning: 'm_bStoppingApp' is the flag of a phase, {{.*}}
		mp_State = {};
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: assigning the object overwrites 'm_bStoppingApp', the flag of a phase, {{.*}}
	}

	DMibActorEntries(Destroy) TCMap<CStr, CDownload> mp_Items;
	DMibActorEntries(Owner) TCMap<CStr, CDownload> mp_Owned;
};

// Round 23: arguments of a lambda's call operator, aggregates with reference
// members, conditionals, inherited flags, and collections of different members
// of the own actor.
struct CRound23State
{
	DMibActorEntries(Owner) TCMap<CStr, CDownload> m_Items;
};

struct CDerivedState : CDistributedAppActor::CState
{
};

struct CRound23 : CDistributedAppActor
{
	void f_Mutable(bool _bLeft)
	{
		[](bool &o_bFlag) { o_bFlag = false; }(mp_State.m_bStoppingApp);
// CHECK-MESSAGES: :[[@LINE-1]]:42: warning: 'm_bStoppingApp' is the flag of a phase, {{.*}}
		[](TCMap<CStr, CDownload> &o_Items, int) { o_Items.f_Clear(); }(mp_Items, 0);
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: 'mp_Items' is declared to lose entries only in Destroy, {{.*}}
		struct CAlias
		{
			bool &m_bFlag;
		};
		CAlias Alias{mp_State.m_bStoppingApp};
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'm_bStoppingApp' is the flag of a phase, {{.*}}
		(_bLeft ? mp_Items : mp_Other).f_Clear();
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: 'mp_Items' is declared to lose entries only in Destroy, {{.*}}
// CHECK-MESSAGES: :[[@LINE-2]]:3: warning: 'mp_Other' is declared to lose entries only in Destroy, {{.*}}
	}

	void f_Derived(CDerivedState &o_State)
	{
		o_State = {};
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: assigning the object overwrites 'm_bStoppingApp', {{.*}}
	}

	void f_OtherMember()
	{
		auto Key = fg_RandomID(m_Left.m_Items);
		m_Left.m_Items[Key];
		m_Right.m_Items.f_Remove(Key);
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: 'm_Items' is declared to lose entries only by the calls that added them, {{.*}}
		m_Left.m_Items.f_Remove(Key);
	}

	DMibActorEntries(Destroy) TCMap<CStr, CDownload> mp_Items;
	DMibActorEntries(Destroy) TCMap<CStr, CDownload> mp_Other;
	CRound23State m_Left;
	CRound23State m_Right;
};

// Round 24: a pointer to a phase function called on another object, or
// escaping, an object that holds an annotated collection assigned, and an
// iterator given to a function.
struct CRound24Storage
{
	DMibActorEntries() TCMap<CStr, CDownload> m_Items;
};

void fg_EraseThrough(TCMap<CStr, CDownload>::CIterator _Iterator);

struct CRound24 : CPhaseActor
{
	DMibActorPhase(Destroy) void f_DestroyOnly();

	DMibActorPhase(Destroy) void f_PhaseMembers(CRound24 *_pOther)
	{
		(this->*&CRound24::f_DestroyOnly)();
		(_pOther->*&CRound24::f_DestroyOnly)();
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'f_DestroyOnly' runs only in the Destroy phase of its actor, but is used here on another object, {{.*}}
	}

	void f_Assign()
	{
		m_Storage = {};
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: 'm_Items' is declared to never lose entries, but is changed here in 'f_Assign'; {{.*}}
		fg_EraseThrough(mp_Items.f_GetIterator());
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: 'mp_Items' is declared to lose entries only in Destroy, {{.*}}
	}

	CRound24Storage m_Storage;
	DMibActorEntries(Destroy) TCMap<CStr, CDownload> mp_Items;
};

// Round 25: explicit casts, free operators, captures changed in a mutable
// lambda, and overrides of functions of several phases.
void operator >> (TCMap<CStr, CDownload> &o_Items, int _Value);

struct CDestroyBase : virtual CActor
{
	DMibActorPhase(Destroy) virtual void f_Cleanup();
};

struct CStopBase : virtual CActor
{
	DMibActorPhase(StopApp) virtual void f_Cleanup();
};

struct CRound25 : CDistributedAppActor, CDestroyBase, CStopBase
{
	DMibActorPhase(StopApp) void f_StopOnly();

	void f_Casts()
	{
		static_cast<TCMap<CStr, CDownload> &>(mp_Items).f_Clear();
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: 'mp_Items' is declared to lose entries only in Destroy, {{.*}}
		static_cast<bool &>(mp_State.m_bStoppingApp) = false;
// CHECK-MESSAGES: :[[@LINE-1]]:48: warning: 'm_bStoppingApp' is the flag of a phase, {{.*}}
		mp_Items >> 0;
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: 'mp_Items' is declared to lose entries only in Destroy, {{.*}}
	}

	DMibActorPhase(StopApp) void f_Mutable(CRound25 *_pOther)
	{
		auto *pThis = this;
		auto fCall = [pThis, _pOther]() mutable
			{
				pThis = _pOther;
				pThis->f_StopOnly();
// CHECK-MESSAGES: :[[@LINE-1]]:12: warning: 'f_StopOnly' runs only in the StopApp phase of its actor, but is used here on another object, {{.*}}
			}
		;
		fCall();
	}

	void f_Cleanup() override
	{
		mp_Items.f_Clear();
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: 'mp_Items' is declared to lose entries only in Destroy, but is changed here in 'f_Cleanup'; {{.*}}
	}

	DMibActorEntries(Destroy) TCMap<CStr, CDownload> mp_Items;
};

// Round 26: an override annotated with another phase, a flag returned by a
// non-const reference, a pointer to an iterator, a reassigned pointer to the
// object, and commas.
struct CRound26 : CDistributedAppActor
{
	DMibActorPhase(Destroy) TCFuture<int> fp_StopApp() override
	{
		mp_Items.f_Clear();
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: 'mp_Items' is declared to lose entries only in Destroy, but is changed here in 'fp_StopApp'; {{.*}}
		co_return 0;
	}

	bool &f_Flag()
	{
		return mp_State.m_bStoppingApp;
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: 'm_bStoppingApp' is the flag of a phase, {{.*}}
	}

	void f_IteratorPointer()
	{
		auto iEntry = mp_Items.f_GetIterator();
		auto *piEntry = &iEntry;
		piEntry->f_Remove();
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: 'mp_Items' is declared to lose entries only in Destroy, {{.*}}
		(0, mp_Items).f_Clear();
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: 'mp_Items' is declared to lose entries only in Destroy, {{.*}}
	}

	void f_Reassigned(CRound26 *_pApp, CRound26 *_pVictim)
	{
		auto Key = fg_RandomID(_pApp->mp_Owned);
		_pApp->mp_Owned[Key];
		_pApp = _pVictim;
		_pApp->mp_Owned.f_Remove(Key);
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: 'mp_Owned' is declared to lose entries only by the calls that added them, {{.*}}
	}

	DMibActorEntries(Destroy) TCMap<CStr, CDownload> mp_Items;
	DMibActorEntries(Owner) TCMap<CStr, CDownload> mp_Owned;
};

// Round 27: co_return of a writable flag or collection, a changed pThis, a
// pointer to the collection given to a function, an iterator assigned,
// aggregates with bases, and an atomic flag set.
void fg_ClearThrough(TCMap<CStr, CDownload> *_pItems);

struct CAggregateBase
{
};

struct CMapAlias : CAggregateBase
{
	TCMap<CStr, CDownload> &m_Items;
};

struct CAtomicFlag
{
	CAtomicFlag &operator = (bool _bValue);
};

struct CRound27 : CDistributedAppActor
{
	TCFuture<bool &> f_FlagFuture()
	{
		co_return mp_State.m_bStoppingApp;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'm_bStoppingApp' is the flag of a phase, {{.*}}
	}

	TCFuture<TCMap<CStr, CDownload> &> f_ItemsFuture()
	{
		co_return mp_Items;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'mp_Items' is declared to lose entries only in Destroy, {{.*}}
	}

	void f_ChangedThis(CRound27 *_pOther)
	{
		auto *pThis = this;
		auto Key = fg_RandomID(pThis->mp_Owned);
		pThis->mp_Owned[Key];
		pThis = _pOther;
		pThis->mp_Owned.f_Remove(Key);
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: 'mp_Owned' is declared to lose entries only by the calls that added them, {{.*}}
	}

	void f_Escapes()
	{
		auto *pItems = &mp_Items;
		fg_ClearThrough(pItems);
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: 'mp_Items' is declared to lose entries only in Destroy, {{.*}}
		decltype(mp_Items.f_GetIterator()) iEntry;
		iEntry = mp_Items.f_GetIterator();
		iEntry.f_Remove();
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: 'mp_Items' is declared to lose entries only in Destroy, {{.*}}
		CMapAlias Alias{{}, mp_Items};
// CHECK-MESSAGES: :[[@LINE-1]]:18: warning: 'mp_Items' is declared to lose entries only in Destroy, {{.*}}
		m_bAtomic = true;
	}

	DMibActorEntries(Destroy) TCMap<CStr, CDownload> mp_Items;
	DMibActorEntries(Owner) TCMap<CStr, CDownload> mp_Owned;
	DMibActorPhaseFlag(Destroy) CAtomicFlag m_bAtomic;
};

struct CStoredFlag
{
	void store(bool _bValue, int _Order = 0);
	bool exchange(bool _bValue);
	bool load() const;
	CStoredFlag &operator ++ ();
};

struct CRound28 : CActor
{
	void f_Flags()
	{
		m_bStored.store(true);
		m_bStored.exchange(true);
		(void)m_bStored.load();
		m_bStored.store(false);
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'm_bStored' is the flag of a phase, {{.*}}
		m_bStored.exchange(false);
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'm_bStored' is the flag of a phase, {{.*}}
		++m_bStored;
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: 'm_bStored' is the flag of a phase, {{.*}}
	}

	void f_AssignedIterator()
	{
		auto iEntry = mp_Items.f_GetIterator();
		decltype(iEntry) iCopy;
		iCopy = iEntry;
		iCopy.f_Remove();
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: 'mp_Items' is declared to lose entries only in Destroy, {{.*}}
	}

	DMibActorEntries(Destroy) TCMap<CStr, CDownload> mp_Items;
	DMibActorPhaseFlag(Destroy) CStoredFlag m_bStored;
};

struct CRound29 : CDistributedAppActor
{
	// Iterators assigned to each other are followed only so far.
	void f_Cycle()
	{
		auto iA = mp_Items.f_GetIterator();
		auto iB = iA;
		iA = iB;
		iB = iA;
	}

	// A lambda that returns a non-const reference lets its caller change it.
	void f_LambdaReturns()
	{
		auto fItems = [this]() -> TCMap<CStr, CDownload> &
		{
			return mp_Items;
// CHECK-MESSAGES: :[[@LINE-1]]:11: warning: 'mp_Items' is declared to lose entries only in Destroy, {{.*}}
		};
		fItems();
		auto fFlag = [this]() -> bool &
		{
			return mp_State.m_bStoppingApp;
// CHECK-MESSAGES: :[[@LINE-1]]:11: warning: 'm_bStoppingApp' is the flag of a phase, {{.*}}
		};
		fFlag();
	}

	// Removing through a dereferenced pointer to an iterator.
	void f_DereferencedIterator()
	{
		auto iEntry = mp_Items.f_GetIterator();
		auto *piEntry = &iEntry;
		(*piEntry).f_Remove();
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: 'mp_Items' is declared to lose entries only in Destroy, {{.*}}
	}

	// A returned iterator can remove the entry it is at anywhere.
	auto f_ReturnedIterator()
	{
		return mp_Items.f_GetIterator();
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: 'mp_Items' is declared to lose entries only in Destroy, {{.*}}
	}

	auto f_ReturnedIteratorCopy()
	{
		auto iEntry = mp_Items.f_GetIterator();
		return iEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: 'mp_Items' is declared to lose entries only in Destroy, {{.*}}
	}

	DMibActorEntries(Destroy) TCMap<CStr, CDownload> mp_Items;
};

struct CRound29Cast : CPhaseActor
{
	// A cast of `this` to a base is the own object.
	DMibActorPhase(Destroy) void f_DestroyBase()
	{
		(void)static_cast<CPhaseActor *>(this)->fp_Destroy();
		(void)static_cast<CPhaseActor &>(*this).fp_Destroy();
	}
};

struct CDestructedMap
{
	struct CIterator
	{
		~CIterator();
		void f_Remove();
	};
	CIterator f_GetIterator();
};

struct CRound30Base : CActor
{
	virtual void f_Cleanup();
	DMibActorPhase(Destroy) virtual void f_DestroyCleanup();
};

struct CRound30 : CRound30Base
{
	// An iterator with a destructor is a temporary bound to be destroyed.
	void f_DestructedIterator()
	{
		auto iEntry = mp_Destructed.f_GetIterator();
		iEntry.f_Remove();
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: 'mp_Destructed' is declared to lose entries only in Destroy, {{.*}}
	}

	// Assigning by name.
	void f_AssignByName()
	{
		mp_Items.operator=({});
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: 'mp_Items' is declared to lose entries only in Destroy, {{.*}}
	}

	// An override restricted to a phase, of a function that isn't.
	DMibActorPhase(Destroy) void f_Cleanup() override
// CHECK-MESSAGES: :[[@LINE-1]]:31: warning: 'f_Cleanup' runs only in Destroy, but overrides 'CRound30Base::f_Cleanup', {{.*}}
	{
		mp_Items.f_Clear();
	}

	DMibActorPhase(Destroy) void f_DestroyCleanup() override
	{
		mp_Items.f_Clear();
	}

	DMibActorEntries(Destroy) TCMap<CStr, CDownload> mp_Items;
	DMibActorEntries(Destroy) CDestructedMap mp_Destructed;
};

struct CRound30State
{
	DMibActorPhaseFlag(Destroy) bool m_bDestroyed = false;
};

struct CRound30Flags : CActor
{
	void f_Reset()
	{
		m_State.operator=({});
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: {{.*}}
	}

	CRound30State m_State;
};

struct CRound31State
{
	DMibActorEntries(Destroy) TCMap<CStr, CDownload> m_First;
	DMibActorEntries(StopApp) TCMap<CStr, CDownload> m_Second;
};

struct CRound31 : CDistributedAppActor
{
	// Each collection the object holds is replaced.
	DMibActorPhase(Destroy) void f_Reset()
	{
		m_State = {};
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: 'm_Second' is declared to lose entries only in StopApp, {{.*}}
	}

	// A static flag, and what setting the flag gives.
	void f_Static()
	{
		ms_bStopping = false;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'ms_bStopping' is the flag of a phase, {{.*}}
		(m_bDestroyed = true) = false;
// CHECK-MESSAGES: :[[@LINE-1]]:25: warning: 'm_bDestroyed' is the flag of a phase, {{.*}}
		bool &bAlias = (m_bDestroyed = true);
// CHECK-MESSAGES: :[[@LINE-1]]:19: warning: 'm_bDestroyed' is the flag of a phase, {{.*}}
		(void)bAlias;
	}

	CRound31State m_State;
	DMibActorPhaseFlag(StopApp) static bool ms_bStopping;
	DMibActorPhaseFlag(Destroy) bool m_bDestroyed = false;
};

struct CRound32 : CActor
{
	struct CState
	{
		DMibActorPhaseFlag(Destroy) bool m_bDestroyed = false;
		DMibActorEntries(Destroy) TCMap<CStr, CDownload> m_Items;
	} m_State;

	using CIterator = TCMap<CStr, CDownload>::CIterator;
	struct CHolder
	{
		CIterator m_Iterator;
	};

	// Structured bindings of annotated members.
	void f_Bindings()
	{
		auto &[bDestroyed, Items] = m_State;
		bDestroyed = false;
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: 'm_bDestroyed' is the flag of a phase, {{.*}}
		Items.f_Clear();
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: 'm_Items' is declared to lose entries only in Destroy, {{.*}}
	}

	// An iterator kept in an object.
	void f_HeldIterator()
	{
		CHolder Holder{mp_Items.f_GetIterator()};
// CHECK-MESSAGES: :[[@LINE-1]]:18: warning: 'mp_Items' is declared to lose entries only in Destroy, {{.*}}
		Holder.m_Iterator.f_Remove();
	}

	DMibActorEntries(Destroy) TCMap<CStr, CDownload> mp_Items;
};

struct CRound33Phased : virtual CActor
{
	DMibActorPhase(Destroy) virtual void f_Stop();
};

struct CRound33Unrestricted
{
	virtual void f_Stop();
	virtual void f_Later();
};

struct CRound33 : CRound33Phased, CRound33Unrestricted
{
	// An inherited phase, also overriding a function that has none.
	void f_Stop() override;
// CHECK-MESSAGES: :[[@LINE-1]]:7: warning: 'f_Stop' runs only in Destroy, but overrides 'CRound33Unrestricted::f_Stop', {{.*}}
	// A phase annotated on the definition.
	void f_Later() override;
// CHECK-MESSAGES: :[[@LINE-1]]:7: warning: 'f_Later' runs only in Destroy, but overrides 'CRound33Unrestricted::f_Later', {{.*}}

	// Constructing or destroying this object doesn't let another one lose
	// entries.
	CRound33(CRound33 &_Other)
	{
		_Other.mp_Items.f_Clear();
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: 'mp_Items' is declared to lose entries only in Destroy, {{.*}}
		mp_Items.f_Clear();
	}

	~CRound33()
	{
		m_pOther->mp_Items.f_Clear();
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: 'mp_Items' is declared to lose entries only in Destroy, {{.*}}
	}

	DMibActorEntries(Destroy) TCMap<CStr, CDownload> mp_Items;
	CRound33 *m_pOther = nullptr;
};

void CRound33::f_Stop()
{
	mp_Items.f_Clear();
}

DMibActorPhase(Destroy) void CRound33::f_Later()
{
	mp_Items.f_Clear();
}

struct CRound34 : CDistributedAppActor
{
	DMibActorPhase(StopApp) static void fs_Clear(CRound34 *_pThis)
	{
		_pThis->mp_Items.f_Clear();
	}

	// A static function of the phase given another actor.
	DMibActorPhase(StopApp) void f_ClearOther(CRound34 *_pOther)
	{
		fs_Clear(this);
		fs_Clear(_pOther);
// CHECK-MESSAGES: :[[@LINE-1]]:12: warning: 'fs_Clear' runs only in the StopApp phase of its actor, but is given another object here, {{.*}}
	}

	// Pointers to annotated data members.
	void f_MemberPointers()
	{
		this->*&CRound34::m_bStopped = false;
// CHECK-MESSAGES: :[[@LINE-1]]:11: warning: a pointer to 'm_bStopped', the flag of a phase, is taken here, {{.*}}
		(this->*&CRound34::mp_Items).f_Clear();
// CHECK-MESSAGES: :[[@LINE-1]]:12: warning: a pointer to 'mp_Items', a collection declared with DMibActorEntries, is taken here, {{.*}}
		bool const CRound34::*pConst = &CRound34::m_bStopped;
		(void)pConst;
	}

	DMibActorEntries(StopApp) TCMap<CStr, CDownload> mp_Items;
	DMibActorPhaseFlag(StopApp) bool m_bStopped = false;
};

namespace std
{
	template <typename t_CIterator>
	int distance(t_CIterator _First, t_CIterator _Last);
}

struct CRound34Distance : CActor
{
	// A standard algorithm has no collection to remove from.
	int f_Count()
	{
		return std::distance(mp_Items.f_GetIterator(), mp_Items.f_GetIterator());
	}

	DMibActorEntries(Destroy) TCMap<CStr, CDownload> mp_Items;
};

struct CRound35KeyReplace
{
	CRound35KeyReplace(CStr &_Key, CStr const &_Existing);
};

struct CRound35Stream
{
};

struct CRound35 : CDistributedAppActor
{
	// A key a constructor can change isn't fresh, and more removals.
	void f_ReplacedKey(CStr _Existing)
	{
		auto Key = fg_RandomID(mp_Owned);
		CRound35KeyReplace Replace(Key, _Existing);
		mp_Owned[Key];
		mp_Owned.f_Remove(Key);
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: 'mp_Owned' is declared to lose entries only by the calls that added them, {{.*}}
	}

	void f_OtherRemovals(CRound35Stream &_Stream)
	{
		mp_Items.f_TryRemovePointerBasedComparison(mp_Items.f_FindEqual("Key"));
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: 'mp_Items' is declared to lose entries only in StopApp, {{.*}}
		mp_Items.f_Consume(_Stream);
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: 'mp_Items' is declared to lose entries only in StopApp, {{.*}}
	}

	// A pointer to const of a flag can't clear it.
	bool const *f_ConstFlag()
	{
		bool const *pFlag = &mp_State.m_bStoppingApp;
		(void)pFlag;
		return &mp_State.m_bStoppingApp;
	}

	DMibActorEntries(Owner) TCMap<CStr, CDownload> mp_Owned;
	DMibActorEntries(StopApp) TCMap<CStr, CDownload> mp_Items;
};


// A pointer to a static function of the phase can be called by another actor,
// and the constructor and destructor of a derived class change the inherited
// collections of the object being constructed or destroyed.
struct CRound37 : CDistributedAppActor
{
	DMibActorPhase(StopApp) static void fs_Clear(CRound37 *_pThis)
	{
		_pThis->mp_Items.f_Clear();
	}

	DMibActorPhase(StopApp) void f_Save()
	{
		void (*pClear)(CRound37 *) = fs_Clear;
// CHECK-MESSAGES: :[[@LINE-1]]:32: warning: 'fs_Clear' runs only in the StopApp phase of its actor, but a pointer to it is taken here, {{.*}}
		(void)pClear;
	}

	DMibActorEntries(StopApp) TCMap<CStr, CDownload> mp_Items;
};

struct CRound37Derived : CRound37
{
	CRound37Derived()
	{
		mp_Items.f_Clear();
	}
	~CRound37Derived()
	{
		mp_Items.f_Clear();
	}
};

// A moved iterator is still at the entry, and a static flag is one also
// through its definition outside of its class.
struct CRound37Moved : CPhaseActor
{
	void f_Erase()
	{
		auto iOriginal = mp_Items.f_GetIterator();
		auto iMoved = std::move(iOriginal);
		iMoved.f_Remove();
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: 'mp_Items' is declared to lose entries only in Destroy, {{.*}}
	}

	DMibActorEntries(Destroy) TCMap<CStr, CDownload> mp_Items;
	DMibActorPhaseFlag(StopApp) static bool ms_bStopping;
};

bool CRound37Moved::ms_bStopping = false;

void fg_ResetStaticFlag()
{
	CRound37Moved::ms_bStopping = false;
// CHECK-MESSAGES: :[[@LINE-1]]:30: warning: 'ms_bStopping' is the flag of a phase, {{.*}}
}

// An iterator made from an iterator, a reference bound by
// parenthesized aggregate initialization, and collections and flags in arrays.
struct CEdgeFlagAlias
{
	bool &m_Flag;
};
struct CEdgeMapAlias
{
	TCMap<int, int> &m_Items;
};
struct CEdgeEntryState
{
	DMibActorEntries(Destroy) TCMap<int, int> m_Items;
};
struct CEdgeState
{
	CEdgeEntryState m_Entries[1];
};
struct CEdgeApp : CDistributedAppActor
{
	DMibActorEntries(StopApp) TCMap<int, int> mp_Items;
	CEdgeState mp_State38;

	void f_RemoveThroughCopy()
	{
		mp_Items.f_GetIterator().f_GetIterator().f_Remove();
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: 'mp_Items' is declared to lose entries only in StopApp, but is changed here in 'f_RemoveThroughCopy'; change it in a function of that phase (DMibActorPhase) [malterlib-actor-phase]
	}
	void f_ResetFlag()
	{
		CEdgeFlagAlias Alias(mp_State.m_bStoppingApp);
// CHECK-MESSAGES: :[[@LINE-1]]:24: warning: 'm_bStoppingApp' is the flag of a phase, which is only ever set to true, so that what runs in the phase can rely on it staying set [malterlib-actor-phase]
		Alias.m_Flag = false;
	}
	void f_ClearThroughAlias()
	{
		CEdgeMapAlias Alias(mp_Items);
// CHECK-MESSAGES: :[[@LINE-1]]:22: warning: 'mp_Items' is declared to lose entries only in StopApp, but is changed here in 'f_ClearThroughAlias'; change it in a function of that phase (DMibActorPhase) [malterlib-actor-phase]
		Alias.m_Items.f_Clear();
	}
	void f_ReplaceArray()
	{
		mp_State38 = {};
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: 'm_Items' is declared to lose entries only in Destroy, but is changed here in 'f_ReplaceArray'; change it in a function of that phase (DMibActorPhase) [malterlib-actor-phase]
	}
};

// A default member initializer runs where its object is made, outside of
// the phases of another actor.
struct CDefaultInitActor : CPhaseActor
{
	DMibActorEntries(Destroy) TCMap<int, int> mp_Items;
};
extern CDefaultInitActor *g_pDefaultInitActor;
struct CResetInInitializer
{
	int m_Dummy = (g_pDefaultInitActor->mp_Items.f_Clear(), 0);
// CHECK-MESSAGES: :[[@LINE-1]]:17: warning: 'mp_Items' is declared to lose entries only in Destroy, but is changed here in 'm_Dummy'; change it in a function of that phase (DMibActorPhase) [malterlib-actor-phase]
};

// An iterator chosen by a conditional, and one whose removal is called
// through a pointer to it.
struct CConditionalIterators : CDistributedAppActor
{
	DMibActorEntries(Destroy, StopApp) TCMap<CStr, int> mp_Items;

	void f_RemoveChosen(bool _bChooseA)
	{
		auto Iterator = _bChooseA ? mp_Items.f_GetIterator("a") : mp_Items.f_GetIterator("b");
		if (Iterator)
			Iterator.f_Remove();
// CHECK-MESSAGES: :[[@LINE-1]]:4: warning: 'mp_Items' is declared to lose entries only in Destroy and StopApp, but is changed here in 'f_RemoveChosen'; change it in a function of those phases (DMibActorPhase) [malterlib-actor-phase]
	}

	void f_RemoveThroughPointer()
	{
		auto Iterator = mp_Items.f_GetIterator();
		(Iterator.*&decltype(Iterator)::f_Remove)();
// CHECK-MESSAGES: :[[@LINE-1]]:4: warning: 'mp_Items' is declared to lose entries only in Destroy and StopApp, but is changed here in 'f_RemoveThroughPointer'; change it in a function of those phases (DMibActorPhase) [malterlib-actor-phase]
	}
};

// A member function of a collection called through a pointer to it, an
// iterator kept by parenthesized aggregate initialization, and a phase flag
// whose address is only tested.
struct CIndirectRemovals : CActor
{
	DMibActorEntries() TCMap<CStr, int> mp_Kept;
	DMibActorEntries(Destroy) TCMap<int, int> mp_Items;
	struct CHolder
	{
		TCMap<int, int>::CIterator m_Iterator;
	};
	DMibActorPhaseFlag(StopApp) bool mp_bFlag = false;

	void f_ClearThroughPointer()
	{
		auto fClear = &TCMap<CStr, int>::f_Clear;
		(mp_Kept.*fClear)();
// CHECK-MESSAGES: :[[@LINE-1]]:4: warning: 'mp_Kept' is declared to never lose entries, but is changed here in 'f_ClearThroughPointer'; remove its entries only in its constructor or destructor [malterlib-actor-phase]
	}

	void f_KeepIterator()
	{
		CHolder Holder(mp_Items.f_GetIterator());
// CHECK-MESSAGES: :[[@LINE-1]]:18: warning: 'mp_Items' is declared to lose entries only in Destroy, but is changed here in 'f_KeepIterator'; change it in a function of that phase (DMibActorPhase) [malterlib-actor-phase]
		Holder.m_Iterator.f_Remove();
	}

	bool f_TestAddress()
	{
		return &mp_bFlag;
	}
};

// An iterator through std::addressof, a collection copied by a lambda, and a
// pointer pointed elsewhere.
namespace std
{
	template <typename t_CType>
	t_CType *addressof(t_CType &_Value) noexcept;
}
struct CAliasEdges : CDistributedAppActor
{
	DMibActorEntries(StopApp) TCMap<CStr, int> mp_Items;

	void f_RemoveThroughAddress()
	{
		auto Iterator = mp_Items.f_GetIterator();
		auto *pIterator = std::addressof(Iterator);
		pIterator->f_Remove();
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: 'mp_Items' is declared to lose entries only in StopApp, but is changed here in 'f_RemoveThroughAddress'; change it in a function of that phase (DMibActorPhase) [malterlib-actor-phase]
	}

	void f_ClearCopy()
	{
		auto &Items = mp_Items;
		[Items]() mutable
			{
				Items.f_Clear();
			}
		();
	}

	void f_ClearElsewhere()
	{
		TCMap<CStr, int> Local;
		auto *pItems = &mp_Items;
		pItems = &Local;
		pItems->f_Clear();
	}

	void f_ClearMaybeElsewhere(bool _bElsewhere)
	{
		TCMap<CStr, int> Local;
		auto *pItems = &mp_Items;
		if (_bElsewhere)
			pItems = &Local;
		pItems->f_Clear();
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: 'mp_Items' is declared to lose entries only in StopApp, but is changed here in 'f_ClearMaybeElsewhere'; change it in a function of that phase (DMibActorPhase) [malterlib-actor-phase]
	}

	void f_ClearBeforeElsewhere()
	{
		auto *pItems = &mp_Items;
		pItems->f_Clear();
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: 'mp_Items' is declared to lose entries only in StopApp, but is changed here in 'f_ClearBeforeElsewhere'; change it in a function of that phase (DMibActorPhase) [malterlib-actor-phase]
		pItems = nullptr;
	}

	void f_ClearThroughCopiedPointer()
	{
		auto *pItems = &mp_Items;
		[pItems]
			{
				pItems->f_Clear();
// CHECK-MESSAGES: :[[@LINE-1]]:5: warning: 'mp_Items' is declared to lose entries only in StopApp, but is changed here in 'f_ClearThroughCopiedPointer'; change it in a function of that phase (DMibActorPhase) [malterlib-actor-phase]
			}
		();
	}

	void f_ClearThroughAssignedPointer()
	{
		auto *pAlias = &mp_Items;
		TCMap<CStr, int> *pItems = nullptr;
		pItems = pAlias;
		pItems->f_Clear();
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: 'mp_Items' is declared to lose entries only in StopApp, but is changed here in 'f_ClearThroughAssignedPointer'; change it in a function of that phase (DMibActorPhase) [malterlib-actor-phase]
	}
};

// An iterator made another one before it removes.
struct CReassignedIterator : CDistributedAppActor
{
	DMibActorEntries(StopApp) TCMap<CStr, int> mp_Items;
	TCMap<CStr, int> mp_Other;

	void f_RemoveFromOther()
	{
		auto iEntry = mp_Items.f_GetIterator();
		iEntry = mp_Other.f_GetIterator();
		iEntry.f_Remove();
	}
};

// The removals of the standard lists.
namespace std
{
	template <typename t_CType>
	struct list
	{
		void remove(t_CType const &_Value);
		void assign(unsigned long _nValues, t_CType const &_Value);
		t_CType &front();
	};
}
struct CStandardList : CDistributedAppActor
{
	DMibActorEntries(StopApp) std::list<int> mp_Items;

	void f_Remove()
	{
		mp_Items.remove(1);
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: 'mp_Items' is declared to lose entries only in StopApp, but is changed here in 'f_Remove'; change it in a function of that phase (DMibActorPhase) [malterlib-actor-phase]
		mp_Items.assign(0, 0);
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: 'mp_Items' is declared to lose entries only in StopApp, but is changed here in 'f_Remove'; change it in a function of that phase (DMibActorPhase) [malterlib-actor-phase]
	}
};

// Moving out an object that holds a collection, an iterator made from an
// iterator, and a binding of a copy.
struct CMovedHolder : CDistributedAppActor
{
	struct CHolderState
	{
		DMibActorEntries(StopApp) TCMap<CStr, int> m_Items;
	};
	CHolderState mp_Data;

	void f_MoveOut()
	{
		CHolderState Old = static_cast<CHolderState &&>(mp_Data);
// CHECK-MESSAGES: :[[@LINE-1]]:22: warning: 'm_Items' is declared to lose entries only in StopApp, but is changed here in 'f_MoveOut'; change it in a function of that phase (DMibActorPhase) [malterlib-actor-phase]
	}

	void f_MoveOutThroughReference()
	{
		auto &Data = mp_Data;
		CHolderState Old = static_cast<CHolderState &&>(Data);
// CHECK-MESSAGES: :[[@LINE-1]]:22: warning: 'm_Items' is declared to lose entries only in StopApp, but is changed here in 'f_MoveOutThroughReference'; change it in a function of that phase (DMibActorPhase) [malterlib-actor-phase]
	}

	void f_RemoveThroughCopy()
	{
		auto iEntry = mp_Data.m_Items.f_GetIterator();
		auto iCopy = iEntry.f_GetIterator();
		iCopy.f_Remove();
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: 'm_Items' is declared to lose entries only in StopApp, but is changed here in 'f_RemoveThroughCopy'; change it in a function of that phase (DMibActorPhase) [malterlib-actor-phase]
	}

	void f_ClearCopiedBinding()
	{
		auto [Items] = mp_Data;
		Items.f_Clear();
	}
};

// An iterator given to a lambda, which can remove the entry it is at.
struct CIteratorToLambda : CDistributedAppActor
{
	DMibActorEntries(StopApp) TCMap<CStr, int> mp_Items;

	void f_Erase()
	{
		auto fRemove = [](auto _iEntry)
			{
				_iEntry.f_Remove();
			}
		;
		fRemove(mp_Items.f_GetIterator());
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: 'mp_Items' is declared to lose entries only in StopApp, but is changed here in 'f_Erase'; change it in a function of that phase (DMibActorPhase) [malterlib-actor-phase]
		auto iEntry = mp_Items.f_GetIterator();
		fRemove(iEntry);
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: 'mp_Items' is declared to lose entries only in StopApp, but is changed here in 'f_Erase'; change it in a function of that phase (DMibActorPhase) [malterlib-actor-phase]
	}
};

// An iterator swapped with another, which then is at the entry.
namespace std
{
	template <typename t_CType>
	void swap(t_CType &_Left, t_CType &_Right) noexcept;
}
struct CSwappedIterator : CDistributedAppActor
{
	DMibActorEntries(StopApp) TCMap<CStr, int> mp_Items;

	void f_Erase()
	{
		auto iEntry = mp_Items.f_GetIterator();
		decltype(iEntry) iOther;
		std::swap(iEntry, iOther);
		iOther.f_Remove();
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: 'mp_Items' is declared to lose entries only in StopApp, but is changed here in 'f_Erase'; change it in a function of that phase (DMibActorPhase) [malterlib-actor-phase]
	}
};

// Replacing an owning pointer destroys what it owns, and with it the entries of
// its collections, but not a handle to an actor; a pointer pointed again at
// what may still be the collection, or past by a jump, still points to it.
struct CRound51State
{
	DMibActorEntries(StopApp) TCMap<CStr, CDownload> m_Items;
};
struct CRound51 : CDistributedAppActor
{
	NStorage::TCSharedPointer<CRound51State> mp_pState;
	TCActor<CRound51> mp_Other;
	DMibActorEntries(StopApp) TCMap<CStr, CDownload> mp_Items;

	void f_Reset()
	{
		mp_pState = nullptr;
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: 'm_Items' is declared to lose entries only in StopApp, {{.*}}
		mp_Other = TCActor<CRound51>();
	}

	void f_Conditional(bool _bChoose)
	{
		TCMap<CStr, CDownload> Local;
		auto *pItems = &mp_Items;
		pItems = _bChoose ? pItems : &Local;
		pItems->f_Clear();
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: 'mp_Items' is declared to lose entries only in StopApp, {{.*}}
	}

	void f_Jump(bool _bSkip)
	{
		TCMap<CStr, CDownload> Local;
		auto *pItems = &mp_Items;
		if (_bSkip)
			goto ClearNow;
		pItems = &Local;
	ClearNow:
		pItems->f_Clear();
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: 'mp_Items' is declared to lose entries only in StopApp, {{.*}}
	}

	void f_Repointed()
	{
		TCMap<CStr, CDownload> Local;
		auto *pItems = &mp_Items;
		pItems = &Local;
		pItems->f_Clear();
	}
};

// A copy of a pointer to the collection kept before still points to it, and a
// collection held in an element of a member array is of the own actor.
struct CRound53State
{
	DMibActorEntries(StopApp) TCMap<CStr, CDownload> m_Items;
};
struct CRound53 : CDistributedAppActor
{
	DMibActorEntries(StopApp) TCMap<CStr, CDownload> mp_Items;
	CRound53State mp_States[2];

	void f_Saved(bool _bChoose)
	{
		TCMap<CStr, CDownload> Local;
		auto *pItems = &mp_Items;
		auto *pSaved = pItems;
		pItems = _bChoose ? pSaved : &Local;
		pItems->f_Clear();
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: 'mp_Items' is declared to lose entries only in StopApp, {{.*}}
	}

	DMibActorPhase(StopApp) void f_ClearArray()
	{
		mp_States[0].m_Items.f_Clear();
	}
};

// Replacing an owning pointer to a holder of a phase flag replaces the flag,
// a function of the standard library that calls what it is given can remove
// through an iterator, and swapping replaces both objects.
namespace std
{
	template <typename t_FFunction, typename t_CArgument>
	void async(t_FFunction &&_fFunction, t_CArgument &&_Argument);
	template <typename t_CType>
	void swap(t_CType &_Left, t_CType &_Right) noexcept;
}
struct CRound54FlagState
{
	DMibActorPhaseFlag(StopApp) bool m_bStopping = false;
};
struct CRound54State
{
	DMibActorEntries(Destroy) TCMap<CStr, CDownload> m_Items;
};
struct CRound54 : CPhaseActor
{
	NStorage::TCSharedPointer<CRound54FlagState> mp_pFlags;
	DMibActorEntries(Destroy) TCMap<CStr, CDownload> mp_Items;
	CRound54State mp_State;

	void f_ReplaceFlags()
	{
		mp_pFlags = nullptr;
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: assigning the object overwrites 'm_bStopping', {{.*}}
	}

	void f_RemoveAsync()
	{
		std::async([](TCMap<CStr, CDownload>::CIterator _iEntry) { _iEntry.f_Remove(); }, mp_Items.f_GetIterator());
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: 'mp_Items' is declared to lose entries only in Destroy, {{.*}}
	}

	void f_Swap()
	{
		CRound54State Empty;
		std::swap(mp_State, Empty);
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: 'm_Items' is declared to lose entries only in Destroy, {{.*}}
	}
};

// An iterator given itself, or a copy of itself, still removes its entry.
struct CRound56Iterator : CPhaseActor
{
	DMibActorEntries(Destroy) TCMap<CStr, int> mp_Items;

	void f_Remove()
	{
		auto iEntry = mp_Items.f_GetIterator();
		iEntry = iEntry;
		iEntry.f_Remove();
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: 'mp_Items' is declared to lose entries only in Destroy, {{.*}}
	}
};

// An iterator kept in another object, and clearing a pointer that owns
// annotated collections.
struct CRound57Holder
{
	TCMap<int, int>::CIterator m_Iterator;
	CRound57Holder(TCMap<int, int>::CIterator const &_Iterator) : m_Iterator(_Iterator) {}
};
template <typename t_CType>
struct TCRound57Unique
{
	t_CType *mp_pData = new t_CType;
	~TCRound57Unique()
	{
		delete mp_pData;
	}
	t_CType *operator->() const
	{
		return mp_pData;
	}
	void f_Clear()
	{
		delete mp_pData;
		mp_pData = nullptr;
	}
};
struct CRound57State
{
	DMibActorEntries(Destroy) TCMap<int, int> mp_Items;
};
struct CRound57Actor : CPhaseActor
{
	DMibActorEntries(Destroy) TCMap<int, int> mp_Items;
	TCRound57Unique<CRound57State> mp_pState;

	void f_Kept()
	{
		auto iEntry = mp_Items.f_GetIterator();
		CRound57Holder Holder(iEntry);
// CHECK-MESSAGES: :[[@LINE-1]]:25: warning: 'mp_Items' is declared to lose entries only in Destroy, {{.*}}
		Holder.m_Iterator.f_Remove();
	}

	void f_Reset()
	{
		mp_pState.f_Clear();
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: 'mp_Items' is declared to lose entries only in Destroy, {{.*}}
	}
};

// Exchanging an object that holds annotated collections replaces them, and
// resetting an observer, whose `operator->` doesn't own what it points to,
// doesn't.
struct CRound61State
{
	DMibActorEntries(Destroy) TCMap<int, int> m_Items;
};
struct CRound61Observer
{
	CRound61State *m_pState = nullptr;
	CRound61State *operator->() const
	{
		return m_pState;
	}
};
struct CRound61Actor : CPhaseActor
{
	CRound61State mp_State;
	CRound61Observer mp_Observer;

	void f_Reset()
	{
		auto Old = fg_Exchange(mp_State, CRound61State{});
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: 'm_Items' is declared to lose entries only in Destroy, {{.*}}
	}

	void f_ForgetObserver()
	{
		mp_Observer = {};
	}
};

// Moving out an owning pointer whose object holds only the flag of a phase
// takes the flag.
template <typename t_CType>
struct TCRound62Owner
{
	t_CType *m_pObject = new t_CType;
	TCRound62Owner() = default;
	TCRound62Owner(TCRound62Owner &&o_Other) : m_pObject(o_Other.m_pObject)
	{
		o_Other.m_pObject = nullptr;
	}
	~TCRound62Owner()
	{
		delete m_pObject;
	}
	t_CType *operator->() const
	{
		return m_pObject;
	}
};
struct CRound62FlagState
{
	DMibActorPhaseFlag(StopApp) bool m_bStopping = false;
};
struct CRound62Actor : CActor
{
	TCRound62Owner<CRound62FlagState> mp_pState;

	void f_Take()
	{
		auto Taken = fg_Move(mp_pState);
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: moving the object out takes 'm_bStopping', the flag of a phase, which is only ever set to true, so that what runs in the phase can rely on it staying set [malterlib-actor-phase]
	}
};

// A fresh key changed through a conditional that is assigned isn't the key the
// entry was added with.
struct CRound63Actor : CActor
{
	DMibActorEntries(Owner) TCMap<CStr, int> mp_Items;

	void f_RemoveOther(CStr _Existing, bool _bChoose)
	{
		auto Key = fg_RandomID(mp_Items);
		mp_Items[Key] = 1;
		CStr Spare;
		(_bChoose ? Key : Spare) = _Existing;
		mp_Items.f_Remove(Key);
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: 'mp_Items' is declared to lose entries only by the calls that added them, but is changed here in 'f_RemoveOther'; remove only the entry this call added, by its fresh key [malterlib-actor-phase]
	}
};

// A local pointer assigned the address of a collection is followed, and what
// a branch of an `if constexpr` that is discarded does never runs.
struct CRound64Actor : CPhaseActor
{
	DMibActorEntries(Destroy) TCMap<int, int> mp_Items;

	int f_Read()
	{
		TCMap<int, int> *pItems = nullptr;
		pItems = &mp_Items;
		return (*pItems)[1];
	}

	void f_Discarded()
	{
		if constexpr (false)
			mp_Items.f_Clear();
	}
};

// An iterator assigned a conditional that can keep it is still of the
// collection; one assigned another iterator on every path isn't, also where it
// is returned; and an alias or an iterator used where nothing runs changes
// nothing.
struct CRound66Actor : CPhaseActor
{
	DMibActorEntries(Destroy) TCMap<int, int> mp_Items;
	TCMap<int, int> mp_Other;

	void f_Conditional(bool _bChoose)
	{
		TCMap<int, int> Local;
		auto iEntry = mp_Items.f_GetIterator();
		auto iOther = Local.f_GetIterator();
		iEntry = _bChoose ? iEntry : iOther;
		iEntry.f_Remove();
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: 'mp_Items' is declared to lose entries only in Destroy, but is changed here in 'f_Conditional'; change it in a function of that phase (DMibActorPhase) [malterlib-actor-phase]
	}

	auto f_Replaced()
	{
		auto iEntry = mp_Items.f_GetIterator();
		iEntry = mp_Other.f_GetIterator();
		return iEntry;
	}

	void f_NeverRun()
	{
		auto &Items = mp_Items;
		auto iEntry = mp_Items.f_GetIterator();
		if constexpr (false)
		{
			Items.f_Clear();
			iEntry.f_Remove();
		}
		(void)noexcept(Items.f_Clear());
		using CRemoved = decltype(Items.f_Remove(1));
		(void)noexcept(iEntry.f_Remove());
	}
};

// What an exchange of an iterator returns is the iterator; and the address of
// the collection by std::addressof is an alias of it, like `&mp_Items`.
namespace std
{
	template <typename t_CType, typename t_CNew>
	t_CType exchange(t_CType &_Value, t_CNew &&_New);
}
struct CRound67Actor : CPhaseActor
{
	DMibActorEntries(Destroy) TCMap<int, int> mp_Items;

	void f_Exchanged()
	{
		auto iEntry = mp_Items.f_GetIterator();
		auto iSaved = std::exchange(iEntry, decltype(iEntry){});
		iSaved.f_Remove();
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: 'mp_Items' is declared to lose entries only in Destroy, but is changed here in 'f_Exchanged'; change it in a function of that phase (DMibActorPhase) [malterlib-actor-phase]
	}

	int f_Address()
	{
		auto *pItems = std::addressof(mp_Items);
		return *pItems->f_FindEqual(0);
	}
};

// A pointer to the collection chosen by a conditional, or cast, is still one.
struct CRound68Actor : CPhaseActor
{
	DMibActorEntries(Destroy) TCMap<int, int> mp_Items;

	void f_Conditional(bool _bMember)
	{
		auto *pItems = &mp_Items;
		TCMap<int, int> Local;
		auto *pAlias = _bMember ? pItems : &Local;
		pAlias->f_Clear();
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: 'mp_Items' is declared to lose entries only in Destroy, but is changed here in 'f_Conditional'; change it in a function of that phase (DMibActorPhase) [malterlib-actor-phase]
	}

	void f_Cast()
	{
		auto *pItems = &mp_Items;
		static_cast<decltype(pItems)>(pItems)->f_Clear();
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: 'mp_Items' is declared to lose entries only in Destroy, but is changed here in 'f_Cast'; change it in a function of that phase (DMibActorPhase) [malterlib-actor-phase]
	}
};

// A callback made in a constructor, which runs later, isn't run while the
// object is made.
struct CRound69Actor : CPhaseActor
{
	DMibActorEntries(Destroy) TCMap<int, int> mp_Items;
	NFunction::TCFunction<void ()> m_fClear;
	NFunction::TCFunction<void ()> m_fMemberClear = [this]
		{
			mp_Items.f_Clear();
// CHECK-MESSAGES: :[[@LINE-1]]:4: warning: 'mp_Items' is declared to lose entries only in Destroy, but is changed here in 'm_fMemberClear'; change it in a function of that phase (DMibActorPhase) [malterlib-actor-phase]
		}
	;

	CRound69Actor()
		: m_fClear
		(
			[this]
			{
				mp_Items.f_Clear();
// CHECK-MESSAGES: :[[@LINE-1]]:5: warning: 'mp_Items' is declared to lose entries only in Destroy, but is changed here in 'CRound69Actor'; change it in a function of that phase (DMibActorPhase) [malterlib-actor-phase]
			}
		)
	{
		[this]
			{
				mp_Items.f_Clear();
			}
			()
		;
	}
};

// Moving the state out through an owning pointer, or detaching it, takes its
// collections; clearing a local copy of the state doesn't change the actor's.
struct CRound70State
{
	DMibActorEntries(StopApp) TCMap<int, int> m_Items;
};
template <typename t_CType>
struct TCRound70Owner
{
	~TCRound70Owner();
	t_CType *operator->() const;
	t_CType &operator*() const;
	t_CType *f_Detach();
};
struct CRound70Actor : CDistributedAppActor
{
	TCRound70Owner<CRound70State> mp_pState;
	CRound70State mp_Data;

	void f_MoveOut()
	{
		auto Old = fg_Move(*mp_pState);
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: 'm_Items' is declared to lose entries only in StopApp, but is changed here in 'f_MoveOut'; change it in a function of that phase (DMibActorPhase) [malterlib-actor-phase]
	}

	void f_Detach()
	{
		delete mp_pState.f_Detach();
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: 'm_Items' is declared to lose entries only in StopApp, but is changed here in 'f_Detach'; change it in a function of that phase (DMibActorPhase) [malterlib-actor-phase]
	}

	void f_Copy()
	{
		auto Copy = mp_Data;
		Copy.m_Items.f_Clear();
	}
};

// A lambda held in a local of a constructor that is only called there runs
// there.
struct CRound71Actor : CPhaseActor
{
	DMibActorEntries(Destroy) TCMap<int, int> mp_Items;

	CRound71Actor()
	{
		auto fInitialize = [this]
			{
				mp_Items.f_Clear();
			}
		;
		fInitialize();
	}
};

// Collections inherited from deep in a hierarchy are held too.
struct CRound72State0
{
	DMibActorEntries(Destroy) TCMap<int, int> m_Items;
};
struct CRound72State1 : CRound72State0
{
};
struct CRound72State2 : CRound72State1
{
};
struct CRound72State3 : CRound72State2
{
};
struct CRound72State4 : CRound72State3
{
};
struct CRound72Actor : CPhaseActor
{
	CRound72State4 mp_State;

	void f_Clear()
	{
		mp_State = {};
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: 'm_Items' is declared to lose entries only in Destroy, but is changed here in 'f_Clear'; change it in a function of that phase (DMibActorPhase) [malterlib-actor-phase]
	}
};

// An actor derived from deeply is another actor whatever the depth, and a
// class reached through many paths of shared bases is looked into once.
template <int t_Depth>
struct TCRound73Deep : TCRound73Deep<t_Depth - 1>
{
};
template <>
struct TCRound73Deep<0> : CPhaseActor
{
};
struct CRound73Root
{
	DMibActorPhase(Destroy) void f_DestroyOnly();
};
template <int t_Depth, int t_Index>
struct TCRound73Level
	: virtual TCRound73Level<t_Depth - 1, 0>
	, virtual TCRound73Level<t_Depth - 1, 1>
	, virtual TCRound73Level<t_Depth - 1, 2>
	, virtual TCRound73Level<t_Depth - 1, 3>
{
};
template <int t_Index>
struct TCRound73Level<0, t_Index> : virtual CRound73Root
{
};
struct CRound73Worker : CPhaseActor
{
	TCSharedPointer<TCRound73Deep<20>> mp_pOther;
	TCSharedPointer<TCRound73Level<13, 0>> mp_pData;

	TCFuture<int> fp_Destroy() override
	{
		co_await mp_pOther->fp_Destroy();
// CHECK-MESSAGES: :[[@LINE-1]]:23: warning: 'fp_Destroy' runs only in the Destroy phase of its actor, but is used here on another object, which may not be in that phase [malterlib-actor-phase]
		mp_pData->f_DestroyOnly();
		co_return 0;
	}
};

// Aliases assigned each other in cycles are each looked at once.
struct CRound75Aliases : CActor
{
	DMibActorEntries(Destroy) TCMap<int, int> mp_Items;

	void f_Cycle()
	{
		auto *p0 = &mp_Items;
		auto *p1 = p0;
		auto *p2 = p0;
		auto *p3 = p0;
		auto *p4 = p0;
		p0 = p1; p0 = p2; p0 = p3; p0 = p4;
		p1 = p0; p1 = p2; p1 = p3; p1 = p4;
		p2 = p0; p2 = p1; p2 = p3; p2 = p4;
		p3 = p0; p3 = p1; p3 = p2; p3 = p4;
		p4 = p0; p4 = p1; p4 = p2; p4 = p3;
		p0->f_Clear();
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: 'mp_Items' is declared to lose entries only in Destroy, but is changed here in 'f_Cycle'; change it in a function of that phase (DMibActorPhase) [malterlib-actor-phase]
	}
};

// A pointer conditionally pointed back at the collection can still change it.
struct CRound77Conditional : CDistributedAppActor
{
	DMibActorEntries(StopApp) TCMap<int, int> mp_Items;

	void f_Clear(bool _bActor)
	{
		TCMap<int, int> Local;
		auto *pItems = &mp_Items;
		pItems = &Local;
		if (_bActor)
			pItems = &mp_Items;
		pItems->f_Clear();
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: 'mp_Items' is declared to lose entries only in StopApp, but is changed here in 'f_Clear'; change it in a function of that phase (DMibActorPhase) [malterlib-actor-phase]
	}
};

// Copies of an iterator, however many, refer to the same collection.
struct CRound77Copies : CDistributedAppActor
{
	DMibActorEntries(StopApp) TCMap<int, int> mp_Items;

	void f_Remove()
	{
		auto Iterator0 = mp_Items.f_GetIterator();
		auto Iterator1 = Iterator0;
		auto Iterator2 = Iterator1;
		auto Iterator3 = Iterator2;
		auto Iterator4 = Iterator3;
		auto Iterator5 = Iterator4;
		Iterator5.f_Remove();
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: 'mp_Items' is declared to lose entries only in StopApp, but is changed here in 'f_Remove'; change it in a function of that phase (DMibActorPhase) [malterlib-actor-phase]
	}
};

// Swapping an array of holders replaces the collections of its elements.
struct CRound78State
{
	DMibActorEntries(StopApp) TCMap<int, int> m_Items;
};
struct CRound78Array : CDistributedAppActor
{
	CRound78State mp_States[2];

	void f_Clear()
	{
		CRound78State Empty[2];
		std::swap(mp_States, Empty);
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: 'm_Items' is declared to lose entries only in StopApp, but is changed here in 'f_Clear'; change it in a function of that phase (DMibActorPhase) [malterlib-actor-phase]
	}
};

// Destroying a collection, or what holds one, removes its entries, but not
// in a branch of an `if constexpr` that is discarded, and a flag can be set to
// what is true, or or-ed.
struct CRound79Holder
{
	DMibActorEntries(Destroy) TCMap<int, int> m_Items;
};
struct CRound79Destroy : CPhaseActor
{
	DMibActorEntries(Destroy) TCMap<int, int> mp_Items;
	CRound79Holder mp_Holder;
	DMibActorPhaseFlag(Destroy) bool mp_bDestroyed = false;

	void f_DestroyItems()
	{
		mp_Items.~TCMap();
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: 'mp_Items' is declared to lose entries only in Destroy, but is changed here in 'f_DestroyItems'; change it in a function of that phase (DMibActorPhase) [malterlib-actor-phase]
	}

	void f_DestroyHolder()
	{
		mp_Holder.~CRound79Holder();
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: 'm_Items' is declared to lose entries only in Destroy, but is changed here in 'f_DestroyHolder'; change it in a function of that phase (DMibActorPhase) [malterlib-actor-phase]
	}

	void f_Discarded()
	{
		if constexpr (false)
			mp_Holder = {};
	}

	void f_SetFlag(bool _bOther)
	{
		constexpr bool c_bYes = true;
		mp_bDestroyed = c_bYes;
		mp_bDestroyed |= _bOther;
		mp_bDestroyed = _bOther;
// CHECK-MESSAGES: :[[@LINE-1]]:17: warning: 'mp_bDestroyed' is the flag of a phase, which is only ever set to true, so that what runs in the phase can rely on it staying set [malterlib-actor-phase]
	}
};

// Resetting an owning pointer that inherits its `operator->`.
struct CRound81State
{
	DMibActorEntries(Destroy) TCMap<int, int> m_Items;
};
struct CRound81Owner
{
	CRound81State *m_pState = nullptr;
	~CRound81Owner()
	{
		delete m_pState;
	}
	CRound81State *operator->() const
	{
		return m_pState;
	}
};
struct CRound81DerivedOwner : CRound81Owner
{
	void reset()
	{
		delete m_pState;
		m_pState = nullptr;
	}
};
struct CRound81Inherited : CPhaseActor
{
	CRound81DerivedOwner mp_Owner;

	void f_Erase()
	{
		mp_Owner.reset();
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: 'm_Items' is declared to lose entries only in Destroy, but is changed here in 'f_Erase'; change it in a function of that phase (DMibActorPhase) [malterlib-actor-phase]
	}
};

// A function of a phase given what is part of another actor, and the
// address of a collection given as a pointer to const.
struct CRound82State
{
	DMibActorEntries(Destroy) TCMap<int, int> mp_Items;
};
DMibActorPhase(Destroy) void fg_Round82Clear(CRound82State *_pThis)
{
	_pThis->mp_Items.f_Clear();
}
void fg_Round82Inspect(TCMap<int, int> const *_pItems);
struct CRound82Parts : CPhaseActor
{
	CRound82State m_State;
	CRound82Parts *m_pOther = nullptr;
	DMibActorEntries(Destroy) TCMap<int, int> mp_Items;

	void f_Inspect()
	{
		fg_Round82Inspect(&mp_Items);
	}

	TCFuture<int> fp_Destroy() override
	{
		fg_Round82Clear(&m_State);
		fg_Round82Clear(&m_pOther->m_State);
// CHECK-MESSAGES: :[[@LINE-1]]:19: warning: 'fg_Round82Clear' runs only in the Destroy phase of its actor, but is given another object here, which may not be in that phase [malterlib-actor-phase]
		co_return 0;
	}
};

// Making the object that holds a phase flag again where it is clears the
// flag, and a const alias cast back to non-const can change what it refers
// to.
void *operator new(decltype(sizeof(0)) _Size, void *_pWhere) noexcept;
struct CRound83Remade : CDistributedAppActor
{
	DMibActorEntries(Destroy) TCMap<int, int> mp_Items;

	void f_Reset()
	{
		new (&mp_State) CState{};
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: assigning the object overwrites 'm_bStoppingApp', the flag of a phase, which is only ever set to true, so that what runs in the phase can rely on it staying set [malterlib-actor-phase]
	}

	void f_Clear()
	{
		auto const &Items = mp_Items;
		const_cast<TCMap<int, int> &>(Items).f_Clear();
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: 'mp_Items' is declared to lose entries only in Destroy, but is changed here in 'f_Clear'; change it in a function of that phase (DMibActorPhase) [malterlib-actor-phase]
	}

	void f_Unstop()
	{
		auto const &bStopping = mp_State.m_bStoppingApp;
		const_cast<bool &>(bStopping) = false;
// CHECK-MESSAGES: :[[@LINE-1]]:33: warning: 'm_bStoppingApp' is the flag of a phase, which is only ever set to true, so that what runs in the phase can rely on it staying set [malterlib-actor-phase]
	}

	void f_Read()
	{
		auto const &Items = mp_Items;
		(void)const_cast<TCMap<int, int> const &>(Items);
	}
};

// A const reference to a flag made of itself, and many made of each other,
// are each followed once.
struct CRound84Aliases : CActor
{
	DMibActorPhaseFlag(StopApp) bool m_bStopping = false;

	bool f_Self()
	{
		bool const &bAlias = true ? m_bStopping : bAlias;
		return bAlias;
	}

	bool f_Chain(bool _bFirst)
	{
		bool const &bAlias0 = m_bStopping;
		bool const &bAlias1 = _bFirst ? bAlias0 : bAlias0;
		bool const &bAlias2 = _bFirst ? bAlias1 : bAlias1;
		bool const &bAlias3 = _bFirst ? bAlias2 : bAlias2;
		bool const &bAlias4 = _bFirst ? bAlias3 : bAlias3;
		bool const &bAlias5 = _bFirst ? bAlias4 : bAlias4;
		bool const &bAlias6 = _bFirst ? bAlias5 : bAlias5;
		bool const &bAlias7 = _bFirst ? bAlias6 : bAlias6;
		bool const &bAlias8 = _bFirst ? bAlias7 : bAlias7;
		bool const &bAlias9 = _bFirst ? bAlias8 : bAlias8;
		bool const &bAlias10 = _bFirst ? bAlias9 : bAlias9;
		bool const &bAlias11 = _bFirst ? bAlias10 : bAlias10;
		bool const &bAlias12 = _bFirst ? bAlias11 : bAlias11;
		bool const &bAlias13 = _bFirst ? bAlias12 : bAlias12;
		bool const &bAlias14 = _bFirst ? bAlias13 : bAlias13;
		bool const &bAlias15 = _bFirst ? bAlias14 : bAlias14;
		bool const &bAlias16 = _bFirst ? bAlias15 : bAlias15;
		bool const &bAlias17 = _bFirst ? bAlias16 : bAlias16;
		bool const &bAlias18 = _bFirst ? bAlias17 : bAlias17;
		bool const &bAlias19 = _bFirst ? bAlias18 : bAlias18;
		bool const &bAlias20 = _bFirst ? bAlias19 : bAlias19;
		bool const &bAlias21 = _bFirst ? bAlias20 : bAlias20;
		bool const &bAlias22 = _bFirst ? bAlias21 : bAlias21;
		bool const &bAlias23 = _bFirst ? bAlias22 : bAlias22;
		bool const &bAlias24 = _bFirst ? bAlias23 : bAlias23;
		return bAlias24;
	}
};

// A pointer to const to a flag cast back to non-const can clear it, and a
// const reference to it that is cast where it doesn't run can't.
struct CRound85Pointer : CPhaseActor
{
	DMibActorPhaseFlag(StopApp) bool m_bStop = false;

	void f_Clear()
	{
		bool const *pStop = &m_bStop;
		*const_cast<bool *>(pStop) = false;
// CHECK-MESSAGES: :[[@LINE-1]]:30: warning: 'm_bStop' is the flag of a phase, which is only ever set to true, so that what runs in the phase can rely on it staying set [malterlib-actor-phase]
	}

	bool f_Read()
	{
		bool const *pStop = &m_bStop;
		return *pStop;
	}

	void f_NeverRun()
	{
		bool const &bStop = m_bStop;
		(void)sizeof(const_cast<bool &>(bStop) = false);
		if constexpr (false)
			const_cast<bool &>(bStop) = false;
	}
};

// A local pointer to const of a collection changes it where the const is cast
// away, and so does a copy of a pointer to a flag; one pointed elsewhere
// before it is used doesn't.
struct CRound86Pointers : CDistributedAppActor
{
	DMibActorEntries(Destroy) TCMap<int, int> mp_Items;

	void f_Clear()
	{
		auto const *pItems = &mp_Items;
		const_cast<TCMap<int, int> *>(pItems)->f_Clear();
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: 'mp_Items' is declared to lose entries only in Destroy, but is changed here in 'f_Clear'; change it in a function of that phase (DMibActorPhase) [malterlib-actor-phase]
	}

	void f_Unstop()
	{
		bool const *pStopping = &mp_State.m_bStoppingApp;
		bool const *pCopy = pStopping;
		*const_cast<bool *>(pCopy) = false;
// CHECK-MESSAGES: :[[@LINE-1]]:30: warning: 'm_bStoppingApp' is the flag of a phase, which is only ever set to true, so that what runs in the phase can rely on it staying set [malterlib-actor-phase]
	}

	void f_Kept()
	{
		bool const *pStopping = &mp_State.m_bStoppingApp;
		bool *pWritable = const_cast<bool *>(pStopping);
// CHECK-MESSAGES: :[[@LINE-1]]:21: warning: 'm_bStoppingApp' is the flag of a phase, which is only ever set to true, so that what runs in the phase can rely on it staying set [malterlib-actor-phase]
		(void)pWritable;
	}

	void f_Repointed()
	{
		bool bLocal = true;
		bool const *pStopping = &mp_State.m_bStoppingApp;
		pStopping = &bLocal;
		*const_cast<bool *>(pStopping) = false;
	}
};

// A key changed through a cast to a reference isn't the fresh key.
struct CRound86Key : CPhaseActor
{
	DMibActorEntries(Owner) TCMap<CStr, int> mp_Items;

	void f_Add(CStr _OtherKey)
	{
		auto Key = fg_RandomID(mp_Items);
		mp_Items[Key] = 1;
		static_cast<CStr &>(Key) = _OtherKey;
		mp_Items.f_Remove(Key);
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: 'mp_Items' is declared to lose entries only by the calls that added them, but is changed here in 'f_Add'; remove only the entry this call added, by its fresh key [malterlib-actor-phase]
	}
};

// A pointer to a flag kept as either value of a conditional points at it.
struct CRound87Flag : CActor
{
	DMibActorPhaseFlag(Destroy) bool m_bFlag = false;

	void f_Clear(bool _bChoose)
	{
		m_bFlag = true;
		bool bLocal = false;
		bool const *pFlag = &m_bFlag;
		pFlag = _bChoose ? pFlag : &bLocal;
		*const_cast<bool *>(pFlag) = false;
// CHECK-MESSAGES: :[[@LINE-1]]:30: warning: 'm_bFlag' is the flag of a phase, which is only ever set to true, so that what runs in the phase can rely on it staying set [malterlib-actor-phase]
	}
};

// Moving a holder through a local pointer to it moves its collections, a
// const alias of a const alias can be cast like it, a reference bound to
// what a pointer to a flag points to is the flag, and taking the address of
// a flag with `std::addressof` only to read it changes nothing.
namespace std
{
	template <typename t_CType>
	t_CType *addressof(t_CType &_Value) noexcept;
}
struct CRound88State
{
	DMibActorEntries(Destroy) TCMap<int, int> mp_Items;
};
struct CRound88Actor : CPhaseActor
{
	CRound88State m_State;
	DMibActorEntries(Destroy) TCMap<int, int> mp_Items;
	DMibActorPhaseFlag(Destroy) bool m_bDestroyed = false;

	void f_Move()
	{
		auto *pState = &m_State;
		auto Old = fg_Move(*pState);
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: 'mp_Items' is declared to lose entries only in Destroy, but is changed here in 'f_Move'; change it in a function of that phase (DMibActorPhase) [malterlib-actor-phase]
	}

	void f_Alias()
	{
		auto const &First = mp_Items;
		auto const &Second = First;
		const_cast<TCMap<int, int> &>(Second).f_Clear();
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: 'mp_Items' is declared to lose entries only in Destroy, but is changed here in 'f_Alias'; change it in a function of that phase (DMibActorPhase) [malterlib-actor-phase]
	}

	void f_Reference()
	{
		bool const *pDestroyed = &m_bDestroyed;
		bool const &bAlias = *pDestroyed;
		const_cast<bool &>(bAlias) = false;
// CHECK-MESSAGES: :[[@LINE-1]]:30: warning: 'm_bDestroyed' is the flag of a phase, which is only ever set to true, so that what runs in the phase can rely on it staying set [malterlib-actor-phase]
	}

	bool f_Read()
	{
		bool const *pDestroyed = std::addressof(m_bDestroyed);
		return *pDestroyed;
	}
};

// Many aliases of a collection are each looked at once.
#define DRound89Alias { auto &Items = mp_Items; Items[1] = 1; }
#define DRound89Aliases4 DRound89Alias DRound89Alias DRound89Alias DRound89Alias
#define DRound89Aliases16 DRound89Aliases4 DRound89Aliases4 DRound89Aliases4 DRound89Aliases4
struct CRound89Aliases : CDistributedAppActor
{
	DMibActorEntries(StopApp) TCMap<int, int> mp_Items;

	void f_Aliases()
	{
		DRound89Aliases16 DRound89Aliases16 DRound89Aliases16 DRound89Aliases16
	}
};

// A chain of aliases longer than is followed can change the collection.
#define DRound89Link(d_Name, d_Previous) auto const &d_Name = d_Previous;
#define DRound89Links2(d_Name, d_Previous) DRound89Link(d_Name##a, d_Previous) DRound89Link(d_Name##b, d_Name##a)
#define DRound89Links4(d_Name, d_Previous) DRound89Links2(d_Name##a, d_Previous) DRound89Links2(d_Name##b, d_Name##ab)
#define DRound89Links8(d_Name, d_Previous) DRound89Links4(d_Name##a, d_Previous) DRound89Links4(d_Name##b, d_Name##abb)
#define DRound89Links16(d_Name, d_Previous) DRound89Links8(d_Name##a, d_Previous) DRound89Links8(d_Name##b, d_Name##abbb)
#define DRound89Links32(d_Name, d_Previous) DRound89Links16(d_Name##a, d_Previous) DRound89Links16(d_Name##b, d_Name##abbbb)
#define DRound89Links64(d_Name, d_Previous) DRound89Links32(d_Name##a, d_Previous) DRound89Links32(d_Name##b, d_Name##abbbbb)
struct CRound89Chain : CActor
{
	DMibActorEntries(Destroy) TCMap<int, int> mp_Items;

	void f_Chain()
	{
		DRound89Links64(Items, mp_Items)
		DRound89Link(Last, Itemsbbbbbb)
		const_cast<TCMap<int, int> &>(Last).f_Clear();
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: 'mp_Items' is declared to lose entries only in Destroy, but is changed here in 'f_Chain'; change it in a function of that phase (DMibActorPhase) [malterlib-actor-phase]
	}
};

// What the actor holds by value is made and destroyed with it.
struct CRound90State
{
	DMibActorEntries(Destroy) TCMap<int, int> m_Items;
};
struct CRound90Holder : CActor
{
	CRound90State mp_State;
	CRound90Holder()
	{
		mp_State.m_Items.f_Clear();
	}
	~CRound90Holder()
	{
		mp_State.m_Items.f_Clear();
	}
};

// What a function gives back of the collection it is given by a const
// reference, `fg_Const(mp_Items)`, is the collection.
template <typename t_CType>
t_CType const &fg_Round92Const(t_CType const &_Value) noexcept
{
	return _Value;
}
struct CRound92Const : CActor
{
	DMibActorEntries() TCMap<int, int> mp_Items;

	void f_Clear()
	{
		const_cast<TCMap<int, int> &>(fg_Round92Const(mp_Items)).f_Clear();
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: 'mp_Items' is declared to never lose entries, but is changed here in 'f_Clear'; remove its entries only in its constructor or destructor [malterlib-actor-phase]
	}
};

// Copies of an iterator however many there are, and the object cleared by
// memset, are followed.
#define DRound93Copy(d_To, d_From) auto i##d_To = i##d_From;

struct CRound93IteratorCopies : public CActor
{
	DMibActorEntries(Destroy) TCMap<int, int> mp_Items;

	void f_Remove()
	{
		auto i0 = mp_Items.f_GetIterator();
		DRound93Copy(1,0) DRound93Copy(2,1) DRound93Copy(3,2) DRound93Copy(4,3) DRound93Copy(5,4)
		DRound93Copy(6,5) DRound93Copy(7,6) DRound93Copy(8,7) DRound93Copy(9,8) DRound93Copy(10,9)
		DRound93Copy(11,10) DRound93Copy(12,11) DRound93Copy(13,12) DRound93Copy(14,13) DRound93Copy(15,14)
		DRound93Copy(16,15) DRound93Copy(17,16) DRound93Copy(18,17) DRound93Copy(19,18) DRound93Copy(20,19)
		DRound93Copy(21,20) DRound93Copy(22,21) DRound93Copy(23,22) DRound93Copy(24,23) DRound93Copy(25,24)
		DRound93Copy(26,25) DRound93Copy(27,26) DRound93Copy(28,27) DRound93Copy(29,28) DRound93Copy(30,29)
		DRound93Copy(31,30) DRound93Copy(32,31) DRound93Copy(33,32) DRound93Copy(34,33) DRound93Copy(35,34)
		DRound93Copy(36,35) DRound93Copy(37,36) DRound93Copy(38,37) DRound93Copy(39,38) DRound93Copy(40,39)
		DRound93Copy(41,40) DRound93Copy(42,41) DRound93Copy(43,42) DRound93Copy(44,43) DRound93Copy(45,44)
		DRound93Copy(46,45) DRound93Copy(47,46) DRound93Copy(48,47) DRound93Copy(49,48) DRound93Copy(50,49)
		DRound93Copy(51,50) DRound93Copy(52,51) DRound93Copy(53,52) DRound93Copy(54,53) DRound93Copy(55,54)
		DRound93Copy(56,55) DRound93Copy(57,56) DRound93Copy(58,57) DRound93Copy(59,58) DRound93Copy(60,59)
		DRound93Copy(61,60) DRound93Copy(62,61) DRound93Copy(63,62) DRound93Copy(64,63) DRound93Copy(65,64)
		i65.f_Remove();
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: 'mp_Items' is declared to lose entries only in Destroy, but is changed here in 'f_Remove'; change it in a function of that phase (DMibActorPhase) [malterlib-actor-phase]
	}
};

struct CRound93MemsetFlag : public CActor
{
	struct CState
	{
		DMibActorPhaseFlag(StopApp) bool m_bStopping = false;
	};

	CState mp_State;

	void f_Reset()
	{
		__builtin_memset(&mp_State, 0, sizeof(mp_State));
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: assigning the object overwrites 'm_bStopping', the flag of a phase, which is only ever set to true, so that what runs in the phase can rely on it staying set [malterlib-actor-phase]
	}
};

// A flag given back by what it is given to, `fg_Const`, is the flag too, and
// pointing a pointer to the collection elsewhere through a reference to it,
// or a pointer to it, doesn't change the collection.
template <typename tf_CType>
tf_CType const &fg_Round95Const(tf_CType const &_Value)
{
	return _Value;
}

struct CRound95Flag : CPhaseActor
{
	DMibActorPhaseFlag(Destroy) bool m_bDestroyed = false;

	void f_Reset()
	{
		const_cast<bool &>(fg_Round95Const(m_bDestroyed)) = false;
// CHECK-MESSAGES: :[[@LINE-1]]:53: warning: 'm_bDestroyed' is the flag of a phase, which is only ever set to true, so that what runs in the phase can rely on it staying set [malterlib-actor-phase]
	}
};

struct CRound95Observer : CPhaseActor
{
	DMibActorEntries(Destroy) TCMap<int, int> mp_Items;

	void f_ResetReference()
	{
		auto *pItems = &mp_Items;
		auto *&pAlias = pItems;
		pAlias = nullptr;
	}

	void f_ResetPointer()
	{
		auto *pItems = &mp_Items;
		auto **ppItems = &pItems;
		*ppItems = nullptr;
	}
};

// `std::ranges::swap` swaps objects like `std::swap`.
namespace std::ranges
{
	inline constexpr struct CSwap
	{
		template <typename tf_CType>
		void operator ()(tf_CType &o_Left, tf_CType &o_Right) const;
	} swap{};
}

struct CRound96State
{
	DMibActorEntries(Destroy) TCMap<int, int> m_Items;
	DMibActorPhaseFlag(Destroy) bool m_bDestroyed = false;
};

struct CRound96App : CActor
{
	CRound96State m_State;

	void f_Reset()
	{
		CRound96State Empty;
		std::ranges::swap(m_State, Empty);
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: 'm_Items' is declared to lose entries only in Destroy, but is changed here in 'f_Reset'; change it in a function of that phase (DMibActorPhase) [malterlib-actor-phase]
// CHECK-MESSAGES: :[[@LINE-2]]:3: warning: assigning the object overwrites 'm_bDestroyed', the flag of a phase, which is only ever set to true, so that what runs in the phase can rely on it staying set [malterlib-actor-phase]
	}
};

// An override in an instantiation is checked against what it overrides, and
// another actor's state reached through a local alias is another object.
struct CRound97Base : CActor
{
	virtual void f_Clear() = 0;
};

template <typename t_CBase>
struct TCRound97App : t_CBase
{
	DMibActorEntries(StopApp) TCMap<int, int> mp_Items;
	DMibActorPhase(StopApp) void f_Clear() override
// CHECK-MESSAGES: :[[@LINE-1]]:31: warning: 'f_Clear' runs only in StopApp, but overrides 'CRound97Base::f_Clear', which callers may call in any phase; declare 'CRound97Base::f_Clear' with DMibActorPhase of the same phases [malterlib-actor-phase]
	{
		mp_Items.f_Clear();
	}
};

TCRound97App<CRound97Base> g_Round97App;

struct CRound97Data
{
	DMibActorEntries(StopApp) TCMap<int, int> m_Items;
};

DMibActorPhase(StopApp) void fg_Round97Clear(CRound97Data *_pThis)
{
	_pThis->m_Items.f_Clear();
}

struct CRound97Stopper : CDistributedAppActor
{
	CRound97Data mp_Data;

	DMibActorPhase(StopApp) void f_Stop(CRound97Stopper *_pOther)
	{
		auto *pState = &_pOther->mp_Data;
		fg_Round97Clear(pState);
// CHECK-MESSAGES: :[[@LINE-1]]:19: warning: 'fg_Round97Clear' runs only in the StopApp phase of its actor, but is given another object here, which may not be in that phase [malterlib-actor-phase]
	}
};

// Clearing the entries of a member in a member initializer, while the actor
// is made.
struct CRound98State
{
	DMibActorEntries() TCMap<int, int> m_Items;
};

struct CRound98Made : CActor
{
	CRound98State m_State;
	int m_Dummy = (m_State.m_Items.f_Clear(), 0);
};

// Another actor's state through a cast or a conditional.
struct CRound101State
{
	DMibActorEntries(Destroy) TCMap<int, int> m_Items;
};

DMibActorPhase(Destroy) void fg_Round101Clear(CRound101State *_pThis)
{
	_pThis->m_Items.f_Clear();
}

struct CRound101Other : CPhaseActor
{
	CRound101State m_State;

	DMibActorPhase(Destroy) void f_Destroy(CRound101Other *_pOther, bool _bOwn)
	{
		fg_Round101Clear(_bOwn ? &m_State : &_pOther->m_State);
// CHECK-MESSAGES: :[[@LINE-1]]:20: warning: 'fg_Round101Clear' runs only in the Destroy phase of its actor, but is given another object here, which may not be in that phase [malterlib-actor-phase]
		fg_Round101Clear(static_cast<CRound101State *>(&_pOther->m_State));
// CHECK-MESSAGES: :[[@LINE-1]]:20: warning: 'fg_Round101Clear' runs only in the Destroy phase of its actor, but is given another object here, which may not be in that phase [malterlib-actor-phase]
	}
};

// Writing a flag through a local pointer to what has it.
struct CRound101FlagState
{
	DMibActorPhaseFlag(StopApp) bool m_bStopping = false;
};

struct CRound101Memset : CPhaseActor
{
	CRound101FlagState m_State;

	void f_Reset()
	{
		auto *pState = &m_State;
		__builtin_memset(pState, 0, sizeof(CRound101FlagState));
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: assigning the object overwrites 'm_bStopping', the flag of a phase, which is only ever set to true, so that what runs in the phase can rely on it staying set [malterlib-actor-phase]
	}
};

// A copy of a const pointer to the collection.
struct CRound101ConstCopy : CActor
{
	DMibActorEntries(Destroy) TCMap<int, int> mp_Items;

	void f_Run()
	{
		auto const *pFirst = &mp_Items;
		auto const *pSecond = pFirst;
		const_cast<TCMap<int, int> *>(pSecond)->f_Clear();
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: 'mp_Items' is declared to lose entries only in Destroy, but is changed here in 'f_Run'; change it in a function of that phase (DMibActorPhase) [malterlib-actor-phase]
	}
};

// A cast that keeps the pointer to the flag const.
struct CRound101FlagCast : CActor
{
	DMibActorPhaseFlag(Destroy) bool m_bDestroyed = false;

	void f_Run()
	{
		m_bDestroyed = true;
		bool const *pFirst = &m_bDestroyed;
		bool const *pSecond = static_cast<bool const *>(pFirst);
		*const_cast<bool *>(pSecond) = false;
// CHECK-MESSAGES: :[[@LINE-1]]:32: warning: 'm_bDestroyed' is the flag of a phase, which is only ever set to true, so that what runs in the phase can rely on it staying set [malterlib-actor-phase]
	}
};

// Nested init captures of an iterator, told once each.
#define DRound102Wrap(d_Value) [Copy = d_Value] {}
#define DRound102Wrap2(d_Value) DRound102Wrap(DRound102Wrap(d_Value))
#define DRound102Wrap4(d_Value) DRound102Wrap2(DRound102Wrap2(d_Value))
#define DRound102Wrap8(d_Value) DRound102Wrap4(DRound102Wrap4(d_Value))
#define DRound102Wrap16(d_Value) DRound102Wrap8(DRound102Wrap8(d_Value))

struct CRound102Nested : CActor
{
	DMibActorEntries(Destroy) TCMap<int, int> mp_Items;

	void f_Run()
	{
		auto iItem = mp_Items.f_GetIterator();
		auto fCallback = DRound102Wrap16(DRound102Wrap8(iItem));
	}
};

// Through a pointer that a helper gives back, with the const cast away.
template <typename tf_CType>
tf_CType const *fg_Round103Pointer(tf_CType const &_Value)
{
	return &_Value;
}

struct CRound103Pointer : CPhaseActor
{
	DMibActorPhaseFlag(StopApp) bool m_bStopping = false;
	DMibActorEntries(Destroy) TCMap<int, int> mp_Items;

	void f_Run()
	{
		*const_cast<bool *>(fg_Round103Pointer(m_bStopping)) = false;
// CHECK-MESSAGES: :[[@LINE-1]]:4: warning: 'm_bStopping' is the flag of a phase, which is only ever set to true, so that what runs in the phase can rely on it staying set [malterlib-actor-phase]
		const_cast<TCMap<int, int> *>(fg_Round103Pointer(mp_Items))->f_Clear();
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: 'mp_Items' is declared to lose entries only in Destroy, but is changed here in 'f_Run'; change it in a function of that phase (DMibActorPhase) [malterlib-actor-phase]
	}
};

// Only what a constant condition chooses is given.
struct CRound103State
{
	int m_Value;
};
DMibActorPhase(Destroy) void fg_Round103ClearState(CRound103State *_pState);

struct CRound103Constant : CActor
{
	CRound103State m_State;

	DMibActorPhase(Destroy) void f_Destroy(CRound103Constant *_pOther)
	{
		fg_Round103ClearState(true ? &m_State : &_pOther->m_State);
	}
};

// A pointer to a member to const, whose const is cast away.
struct CRound104MemberPointer : CPhaseActor
{
	DMibActorPhaseFlag(Destroy) bool m_bDestroyed = false;

	void f_Reset()
	{
		bool const CRound104MemberPointer::*pFlag = &CRound104MemberPointer::m_bDestroyed;
// CHECK-MESSAGES: :[[@LINE-1]]:48: warning: a pointer to 'm_bDestroyed', the flag of a phase, is taken here, through which it can be changed anywhere; use the member directly, or take a pointer to const [malterlib-actor-phase]
		this->*const_cast<bool CRound104MemberPointer::*>(pFlag) = false;
	}
};

// A pointer to const pointed elsewhere first.
struct CRound104Repointed : CPhaseActor
{
	DMibActorEntries(Destroy) TCMap<int, int> mp_Items;

	void f_ClearLocal()
	{
		TCMap<int, int> Local;
		auto const *pItems = &mp_Items;
		pItems = &Local;
		const_cast<TCMap<int, int> *>(pItems)->f_Clear();
	}
};

// A removing operator called by its name.
struct CRound105Map : TCMap<int, int>
{
	CRound105Map &operator-=(CRound105Map const &_Other)
	{
		f_Remove(1);
		return *this;
	}
};

struct CRound105NamedOperator : CPhaseActor
{
	DMibActorEntries(Destroy) CRound105Map mp_Items;

	void f_Remove(CRound105Map const &_Other)
	{
		mp_Items.operator-=(_Other);
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: 'mp_Items' is declared to lose entries only in Destroy, but is changed here in 'f_Remove'; change it in a function of that phase (DMibActorPhase) [malterlib-actor-phase]
	}
};

// A pointer to const assigned to another local, whose const is cast away.
struct CRound105AssignedConstPointer : CPhaseActor
{
	DMibActorEntries(Destroy) TCMap<int, int> mp_Items;

	void f_Clear()
	{
		auto const *pItems = &mp_Items;
		decltype(pItems) pCopy = nullptr;
		pCopy = pItems;
		const_cast<TCMap<int, int> *>(pCopy)->f_Clear();
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: 'mp_Items' is declared to lose entries only in Destroy, but is changed here in 'f_Clear'; change it in a function of that phase (DMibActorPhase) [malterlib-actor-phase]
	}
};

// Getters returning references to const, whose const is cast away.
struct CRound105ConstGetters : CPhaseActor
{
	DMibActorPhaseFlag(StopApp) bool m_bStopping = false;
	DMibActorEntries(StopApp) TCMap<int, int> mp_Items;

	bool const &f_Status() const
	{
		return m_bStopping;
	}

	TCMap<int, int> const &f_Items() const
	{
		return mp_Items;
	}

	void f_Reset()
	{
		const_cast<bool &>(f_Status()) = false;
// CHECK-MESSAGES: :[[@LINE-1]]:34: warning: 'm_bStopping' is the flag of a phase, which is only ever set to true, so that what runs in the phase can rely on it staying set [malterlib-actor-phase]
		const_cast<TCMap<int, int> &>(f_Items()).f_Clear();
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: 'mp_Items' is declared to lose entries only in StopApp, but is changed here in 'f_Reset'; change it in a function of that phase (DMibActorPhase) [malterlib-actor-phase]
	}
};

// An owner entry made through a cast to the base that has the collection.
struct CRound105OwnerBase : CPhaseActor
{
	DMibActorEntries(Owner) TCMap<CStr, int> mp_Items;
};

struct CRound105OwnerCast : CRound105OwnerBase
{
	void f_AddAndRemove()
	{
		auto Key = fg_RandomID(static_cast<CRound105OwnerBase *>(this)->mp_Items);
		mp_Items[Key] = 0;
		mp_Items.f_Remove(Key);
	}
};

// The address of a flag assigned to a pointer to const declared before.
struct CRound107AssignedAddress : CPhaseActor
{
	DMibActorPhaseFlag(Destroy) bool m_bDestroyed = false;

	void f_Clear()
	{
		bool const *pFlag = nullptr;
		pFlag = &m_bDestroyed;
		*const_cast<bool *>(pFlag) = false;
// CHECK-MESSAGES: :[[@LINE-1]]:30: warning: 'm_bDestroyed' is the flag of a phase, which is only ever set to true, so that what runs in the phase can rely on it staying set [malterlib-actor-phase]
	}
};

// A const alias whose const is cast away as either value of a conditional.
struct CRound108Conditional : CPhaseActor
{
	DMibActorEntries(Destroy) TCMap<int, int> mp_Items;

	void f_Clear(bool _bChoose)
	{
		auto const &Alias = mp_Items;
		const_cast<TCMap<int, int> &>(_bChoose ? Alias : Alias).f_Clear();
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: 'mp_Items' is declared to lose entries only in Destroy, but is changed here in 'f_Clear'; change it in a function of that phase (DMibActorPhase) [malterlib-actor-phase]
	}

	void f_Remove(bool _bChoose)
	{
		auto iItem = mp_Items.f_GetIterator();
		(_bChoose ? iItem : iItem).f_Remove();
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: 'mp_Items' is declared to lose entries only in Destroy, but is changed here in 'f_Remove'; change it in a function of that phase (DMibActorPhase) [malterlib-actor-phase]
	}
};

// Another actor's state through the address of a local reference to it, of
// what a pointer points to, and of a pointer assigned it in a branch.
struct CRound109State
{
	DMibActorEntries(Destroy) TCMap<int, int> mp_Items;
};

DMibActorPhase(Destroy) void fg_Round109Clear(CRound109State *_pThis)
{
	_pThis->mp_Items.f_Clear();
}

struct CRound109Other : CPhaseActor
{
	CRound109State m_State;

	DMibActorPhase(Destroy) void f_ThroughReference(CRound109Other *_pOther)
	{
		auto &State = _pOther->m_State;
		fg_Round109Clear(&State);
// CHECK-MESSAGES: :[[@LINE-1]]:20: warning: 'fg_Round109Clear' runs only in the Destroy phase of its actor, but is given another object here, which may not be in that phase [malterlib-actor-phase]
	}

	DMibActorPhase(Destroy) void f_ThroughDereference(CRound109Other *_pOther)
	{
		auto *pState = &_pOther->m_State;
		fg_Round109Clear(&*pState);
// CHECK-MESSAGES: :[[@LINE-1]]:20: warning: 'fg_Round109Clear' runs only in the Destroy phase of its actor, but is given another object here, which may not be in that phase [malterlib-actor-phase]
	}

	DMibActorPhase(Destroy) void f_InBranch(CRound109Other *_pOther, bool _bOther)
	{
		auto *pState = &m_State;
		if (_bOther)
			pState = &_pOther->m_State;
		fg_Round109Clear(pState);
// CHECK-MESSAGES: :[[@LINE-1]]:20: warning: 'fg_Round109Clear' runs only in the Destroy phase of its actor, but is given another object here, which may not be in that phase [malterlib-actor-phase]
	}
};

// Or-ing the flag in, which keeps it set.
struct CRound111Flag : CPhaseActor
{
	DMibActorPhaseFlag(StopApp) bool m_bStoppingApp = false;

	void f_Stop(bool _bOther)
	{
		m_bStoppingApp = m_bStoppingApp || true;
		m_bStoppingApp = _bOther || true;
		m_bStoppingApp = m_bStoppingApp || _bOther;
		m_bStoppingApp = _bOther || m_bStoppingApp;
		m_bStoppingApp = _bOther && m_bStoppingApp;
// CHECK-MESSAGES: :[[@LINE-1]]:18: warning: 'm_bStoppingApp' is the flag of a phase, which is only ever set to true, so that what runs in the phase can rely on it staying set [malterlib-actor-phase]
	}
};

// The collection through a pointer indexed, `p[0]`, or moved by an offset,
// `(p + 0)->`, is the collection.
struct CRound112Indexed : CPhaseActor
{
	DMibActorEntries(Destroy) TCMap<int, int> mp_Items;

	void f_Remove()
	{
		auto *p = &mp_Items;
		p[0].f_Clear();
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: 'mp_Items' is declared to lose entries only in Destroy, but is changed here in 'f_Remove'; change it in a function of that phase (DMibActorPhase) [malterlib-actor-phase]
		(p + 0)->f_Clear();
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: 'mp_Items' is declared to lose entries only in Destroy, but is changed here in 'f_Remove'; change it in a function of that phase (DMibActorPhase) [malterlib-actor-phase]
	}
};

// Round 114: a copy of a pointer to member whose const is cast away
namespace NRound114Copy
{
struct A : CActor {
  DMibActorPhaseFlag(Destroy) bool stopped = false;
  DMibActorEntries(Destroy) TCMap<int, int> items;
  void reset() { bool const A::*pm = &A::stopped; auto copy = pm; this->*const_cast<bool A::*>(copy) = false; }
// CHECK-MESSAGES: :[[@LINE-1]]:39: warning: a pointer to 'stopped', the flag of a phase, is taken here, through which it can be changed anywhere; use the member directly, or take a pointer to const [malterlib-actor-phase]
  void clear() { TCMap<int, int> const A::*pm = &A::items; auto copy = pm; (this->*const_cast<TCMap<int, int> A::*>(copy)).f_Clear(); }
// CHECK-MESSAGES: :[[@LINE-1]]:50: warning: a pointer to 'items', a collection declared with DMibActorEntries, is taken here, through which it can be changed anywhere; use the member directly, or take a pointer to const [malterlib-actor-phase]
};
}

// Round 114: a getter that returns a member through a local reference
namespace NRound114Alias
{
struct A : CActor {
  DMibActorPhaseFlag(Destroy) bool stopped = false;
  DMibActorEntries(Destroy) TCMap<int, int> items;
  bool const &status() const { bool const &alias = stopped; return alias; }
  TCMap<int, int> const &entries() const { auto const &alias = items; return alias; }
  void reset() { const_cast<bool &>(status()) = false; }
// CHECK-MESSAGES: :[[@LINE-1]]:47: warning: 'stopped' is the flag of a phase, which is only ever set to true, so that what runs in the phase can rely on it staying set [malterlib-actor-phase]
  void clear() { const_cast<TCMap<int, int> &>(entries()).f_Clear(); }
// CHECK-MESSAGES: :[[@LINE-1]]:18: warning: 'items' is declared to lose entries only in Destroy, but is changed here in 'clear'; change it in a function of that phase (DMibActorPhase) [malterlib-actor-phase]
};
}

// Round 114: a flag in many parentheses
namespace NRound114Parens
{
struct A : CDistributedAppActor {
  void reset() { ((((((((((((((((mp_State.m_bStoppingApp)))))))))))))))) = false; }
// CHECK-MESSAGES: :[[@LINE-1]]:74: warning: 'm_bStoppingApp' is the flag of a phase, which is only ever set to true, so that what runs in the phase can rely on it staying set [malterlib-actor-phase]
};
}

// Moving an annotated collection out through a reference parameter changes it.
namespace NRound115HolderMove {
struct State {
  DMibActorEntries(Destroy) TCMap<int, int> items;
};
void mutate(State &s) {
  State stolen(fg_Move(s));
// CHECK-MESSAGES: :[[@LINE-1]]:9: warning: 'items' is declared to lose entries only in Destroy, but is changed here in 'mutate'; change it in a function of that phase (DMibActorPhase) [malterlib-actor-phase]
}
struct App : CPhaseActor {
  State state;
  TCFuture<int> f() {
    auto guard = co_await f_CheckDestroyedOnResume();
    int &v = state.items[1];
    mutate(state);
    co_await TCFuture<int>{};
    co_return v;
  }
};
}

// f_ExtractAndInsert only adds to the collection it is called on.
namespace NRound115ExtractAndInsert {
namespace Model {
struct TCMap {
  struct CNodeHandle {};
  int *f_FindEqual(int);
  CNodeHandle f_Extract(int *);
  void f_Insert(CNodeHandle);
  void f_ExtractAndInsert(TCMap &source, int *entry) {
    f_Insert(source.f_Extract(entry));
  }
};
}
struct CMyActor : CActor {
  DMibActorEntries(Destroy) Model::TCMap m_Items;
  void f_Add() {
    Model::TCMap source;
    m_Items.f_ExtractAndInsert(source, source.f_FindEqual(0));
  }
};
}

// Designated initializers bind mutable reference members like positional ones.
namespace NRound116Designated {
struct App : CDistributedAppActor {
  DMibActorEntries(StopApp) TCMap<int, int> items;
  struct Alias {
    bool &flag;
    TCMap<int, int> &map;
  };
  void bad() {
    Alias a{.flag = mp_State.m_bStoppingApp, .map = items};
// CHECK-MESSAGES: :[[@LINE-1]]:12: warning: 'items' is declared to lose entries only in StopApp, but is changed here in 'bad'; change it in a function of that phase (DMibActorPhase) [malterlib-actor-phase]
// CHECK-MESSAGES: :[[@LINE-2]]:21: warning: 'm_bStoppingApp' is the flag of a phase, which is only ever set to true, so that what runs in the phase can rely on it staying set [malterlib-actor-phase]
    a.flag = false;
    a.map.f_Clear();
  }
};
}

// A C-style cast casts const away from a getter like const_cast.
namespace NRound116CStyleCast {
struct App : CDistributedAppActor {
  DMibActorPhaseFlag(StopApp) bool flag;
  DMibActorEntries(StopApp) TCMap<int, int> items;
  bool const &status() const { return flag; }
  TCMap<int, int> const &data() const { return items; }
  void bad() {
    (bool &)status() = false;
// CHECK-MESSAGES: :[[@LINE-1]]:22: warning: 'flag' is the flag of a phase, which is only ever set to true, so that what runs in the phase can rely on it staying set [malterlib-actor-phase]
    ((TCMap<int, int> &)data()).f_Clear();
// CHECK-MESSAGES: :[[@LINE-1]]:5: warning: 'items' is declared to lose entries only in StopApp, but is changed here in 'bad'; change it in a function of that phase (DMibActorPhase) [malterlib-actor-phase]
  }
};
}

// A pointer to an annotated collection given to a constructor lets it remove entries.
namespace NRound117ConstructorPointer {
struct CClear {
  explicit CClear(TCMap<int, int> *p) { p->f_Clear(); }
};
struct CApp : CPhaseActor {
  DMibActorEntries(Destroy) TCMap<int, int> mp_Items;
  void f_Bad() {
    auto *p = &mp_Items;
    CClear Clear(p);
// CHECK-MESSAGES: :[[@LINE-1]]:12: warning: 'mp_Items' is declared to lose entries only in Destroy, but is changed here in 'f_Bad'; change it in a function of that phase (DMibActorPhase) [malterlib-actor-phase]
  }
};
}

// Lambdas nested in default arguments are traversed once by descendant matchers.
namespace NRound117NestedDefaults {
#define L(X) [](int Value = X) { return Value; }(0)
struct CApp : CPhaseActor {
  DMibActorPhaseFlag(StopApp) bool m_bFlag = false;
  void f_Test() {
    bool const &Alias = m_bFlag;
    int Value = L(L(L(L(L(L(L(L(L(L(L(L(7))))))))))));
    (void)Alias;
    (void)Value;
  }
};
}

// An atomic flag stored a constant that is true stays set.
namespace NRound117AtomicConstants {
struct CAtomicBool {
  void store(bool _bValue);
  bool exchange(bool _bValue);
  bool compare_exchange_strong(bool &_bExpected, bool _bDesired);
  CAtomicBool &operator=(bool _bValue);
};
struct CApp : CPhaseActor {
  DMibActorPhaseFlag(StopApp) CAtomicBool m_bStopping;
  void f_Set() {
    constexpr bool Yes = true;
    bool Expected = false;
    m_bStopping.store(Yes);
    m_bStopping.store(!false);
    m_bStopping.exchange(Yes);
    m_bStopping.compare_exchange_strong(Expected, !false);
    m_bStopping = Yes;
    m_bStopping.store(!Yes);
// CHECK-MESSAGES: :[[@LINE-1]]:17: warning: 'm_bStopping' is the flag of a phase, which is only ever set to true, so that what runs in the phase can rely on it staying set [malterlib-actor-phase]
  }
};
}

// What a getter of another actor gives back is of that actor, like its member.
namespace NRound118Getter {
struct CData {
  DMibActorEntries(StopApp) TCMap<int, int> m_Items;
};

DMibActorPhase(StopApp)
void fg_Clear(CData *_pThis) {
  _pThis->m_Items.f_Clear();
}

struct CApp : CDistributedAppActor {
  CData m_Data;
  CData *f_GetData() { return &m_Data; }

  DMibActorPhase(StopApp)
  void f_StopOther(CApp *_pOther) {
    fg_Clear(_pOther->f_GetData());
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: 'fg_Clear' runs only in the StopApp phase of its actor, but is given another object here, which may not be in that phase [malterlib-actor-phase]
  }
};
}

// Round 119: or-ing the flag with what suspends after reading it can clear it.
namespace NRound119FlagOrAwait
{
TCFuture<bool> fg_Work();
struct CApp : CActor {
  DMibActorPhaseFlag(StopApp) bool m_bStopping = false;

  void f_Stop() { m_bStopping = true; }

  TCFuture<int> f_Run() {
    m_bStopping = m_bStopping || co_await fg_Work();
// CHECK-MESSAGES: :[[@LINE-1]]:17: warning: 'm_bStopping' is the flag of a phase, which is only ever set to true, so that what runs in the phase can rely on it staying set [malterlib-actor-phase]
    m_bStopping = co_await fg_Work() || m_bStopping;
    co_return 0;
  }
};
}

// Round 120: changing a collection or a flag through a const pointer to it,
// whose const is cast away from what it points to.
namespace NRound120ConstPointer
{
TCFuture<int> fg_Work();
struct CApp : CPhaseActor {
  DMibActorEntries(Destroy) TCMap<int, int> m_Items;
  DMibActorPhaseFlag(StopApp) bool m_bStopping = false;

  TCFuture<int> f_Run() {
    auto Guard = co_await f_CheckDestroyedOnResume();
    int &Value = m_Items[0];
    auto const *pItems = &m_Items;
    const_cast<decltype(m_Items) &>(*pItems).f_Clear();
// CHECK-MESSAGES: :[[@LINE-1]]:5: warning: 'm_Items' is declared to lose entries only in Destroy, but is changed here in 'f_Run'; change it in a function of that phase (DMibActorPhase) [malterlib-actor-phase]
    co_await fg_Work();
    co_return Value;
  }

  void f_Clear() {
    m_bStopping = true;
    bool const *pStopping = &m_bStopping;
    const_cast<bool &>(pStopping[0]) = false;
// CHECK-MESSAGES: :[[@LINE-1]]:38: warning: 'm_bStopping' is the flag of a phase, which is only ever set to true, so that what runs in the phase can rely on it staying set [malterlib-actor-phase]
  }
};
}

// Round 122: a collection changed through a local reference to what a getter
// gives, and a flag cleared through a conditional, a comma or an offset of a
// pointer to it.
namespace NRound122
{
struct CApp : CPhaseActor {
  DMibActorEntries(Destroy) TCMap<int, int> m_Items;
  DMibActorPhaseFlag(StopApp) bool m_bStopping = false;
  TCMap<int, int> const &f_Items() const { return m_Items; }

  void f_Clear() {
    auto const &Alias = f_Items();
    const_cast<TCMap<int, int> &>(Alias).f_Clear();
// CHECK-MESSAGES: :[[@LINE-1]]:5: warning: 'm_Items' is declared to lose entries only in Destroy, but is changed here in 'f_Clear'; change it in a function of that phase (DMibActorPhase) [malterlib-actor-phase]
  }

  void f_Reset(bool _bChoose) {
    bool const *pStopping = &m_bStopping;
    *const_cast<bool *>(_bChoose ? pStopping : pStopping) = false;
// CHECK-MESSAGES: :[[@LINE-1]]:59: warning: 'm_bStopping' is the flag of a phase, which is only ever set to true, so that what runs in the phase can rely on it staying set [malterlib-actor-phase]
    *const_cast<bool *>((0, pStopping)) = false;
// CHECK-MESSAGES: :[[@LINE-1]]:41: warning: 'm_bStopping' is the flag of a phase, which is only ever set to true, so that what runs in the phase can rely on it staying set [malterlib-actor-phase]
    *const_cast<bool *>(pStopping + 0) = false;
// CHECK-MESSAGES: :[[@LINE-1]]:40: warning: 'm_bStopping' is the flag of a phase, which is only ever set to true, so that what runs in the phase can rely on it staying set [malterlib-actor-phase]
  }
};
}

// Round 124: or-ing the flag with what starts the phase can clear it again, and
// the value a constant condition doesn't select is never used.
namespace NRound124
{
struct CApp : CDistributedAppActor {
  DMibActorEntries(Destroy) TCMap<int, int> mp_Items;

  DMibActorPhaseEntry(StopApp) bool f_Begin() {
    mp_State.m_bStoppingApp = true;
    return false;
  }

  void f_OrStarts() {
    mp_State.m_bStoppingApp = mp_State.m_bStoppingApp || f_Begin();
// CHECK-MESSAGES: :[[@LINE-1]]:29: warning: 'm_bStoppingApp' is the flag of a phase, which is only ever set to true, so that what runs in the phase can rely on it staying set [malterlib-actor-phase]
  }

  void f_OrKeeps(bool _bOther) {
    mp_State.m_bStoppingApp = mp_State.m_bStoppingApp || _bOther;
  }

  void f_Unselected() {
    bool bLocal = false;
    (false ? mp_State.m_bStoppingApp : bLocal) = false;
    TCMap<int, int> Local;
    (false ? mp_Items : Local).f_Clear();
  }

  void f_Selected() {
    bool bLocal = false;
    (true ? mp_State.m_bStoppingApp : bLocal) = false;
// CHECK-MESSAGES: :[[@LINE-1]]:47: warning: 'm_bStoppingApp' is the flag of a phase, which is only ever set to true, so that what runs in the phase can rely on it staying set [malterlib-actor-phase]
  }
};
}

// Round 126: a helper that sets the flag starts the phase, so or-ing the flag
// with it can clear it again.
namespace NRound126
{
struct CApp : CPhaseActor {
  DMibActorPhaseFlag(StopApp) bool m_bStopping = false;

  bool f_SetStopping() {
    m_bStopping = true;
    return false;
  }

  void f_Run() {
    m_bStopping = m_bStopping || f_SetStopping();
// CHECK-MESSAGES: :[[@LINE-1]]:17: warning: 'm_bStopping' is the flag of a phase, which is only ever set to true, so that what runs in the phase can rely on it staying set [malterlib-actor-phase]
  }
};
}

// Round 132: moving either of a conditional out changes it, also through any
// number of references.
namespace NRound132
{
struct CState {
  DMibActorEntries(Destroy) TCMap<int, int> m_Items;
};

struct CApp : CPhaseActor {
  CState mp_First;
  CState mp_Second;

  void f_Conditional(bool _bChoose) {
    CState Stolen(fg_Move(_bChoose ? mp_First : mp_Second));
// CHECK-MESSAGES: :[[@LINE-1]]:12: warning: 'm_Items' is declared to lose entries only in Destroy, but is changed here in 'f_Conditional'; change it in a function of that phase (DMibActorPhase) [malterlib-actor-phase]
  }

  void f_Aliases() {
    CState &a = mp_First, &b = a, &c = b, &d = c, &e = d, &f = e;
    CState Stolen(fg_Move(f));
// CHECK-MESSAGES: :[[@LINE-1]]:12: warning: 'm_Items' is declared to lose entries only in Destroy, but is changed here in 'f_Aliases'; change it in a function of that phase (DMibActorPhase) [malterlib-actor-phase]
  }
};
}

// Round 133: a collection a getter gives a pointer to, also kept in a local,
// is it, and a reference to a pointer to a flag is that pointer.
namespace NRound133
{
struct CApp : CActor {
  DMibActorEntries(Destroy) TCMap<int, int> mp_Items;
  DMibActorPhaseFlag(StopApp) bool mp_bStopped = true;

  TCMap<int, int> const *f_Get() const { return &mp_Items; }

  void f_Clear() { const_cast<TCMap<int, int> &>(*f_Get()).f_Clear(); }
// CHECK-MESSAGES: :[[@LINE-1]]:20: warning: 'mp_Items' is declared to lose entries only in Destroy, but is changed here in 'f_Clear'; change it in a function of that phase (DMibActorPhase) [malterlib-actor-phase]

  void f_ClearLocal() {
    auto pItems = f_Get();
    const_cast<TCMap<int, int> *>(pItems)->f_Clear();
// CHECK-MESSAGES: :[[@LINE-1]]:5: warning: 'mp_Items' is declared to lose entries only in Destroy, but is changed here in 'f_ClearLocal'; change it in a function of that phase (DMibActorPhase) [malterlib-actor-phase]
  }

  void f_Reset() {
    bool const *pStopped = &mp_bStopped;
    bool const *&pAlias = pStopped;
    const_cast<bool &>(*pAlias) = false;
// CHECK-MESSAGES: :[[@LINE-1]]:33: warning: 'mp_bStopped' is the flag of a phase, which is only ever set to true, so that what runs in the phase can rely on it staying set [malterlib-actor-phase]
  }
};
}

// Round 136: any number of unchanged aliases of this are the own object.
namespace NRound136
{
struct CApp : CActor {
  DMibActorPhase(Destroy) void f_Clear() {}

  DMibActorPhase(Destroy) void f_Stop() {
    auto *p0 = this;
    auto *p1 = p0;
    auto *p2 = p1;
    auto *p3 = p2;
    auto *p4 = p3;
    auto *p5 = p4;
    auto *p6 = p5;
    auto *p7 = p6;
    auto *p8 = p7;
    p8->f_Clear();
  }
};
}

namespace NRound138 {
// A static flag named by itself, `ms_bDestroying = ...`, is a phase flag too.
struct CApp : CActor {
  DMibActorPhaseFlag(Destroy) static bool ms_bDestroying;
  static bool fs_Begin() {
    ms_bDestroying = true;
    return false;
  }
  void f_Clear() {
    ms_bDestroying = ms_bDestroying || fs_Begin();
// CHECK-MESSAGES: :[[@LINE-1]]:20: warning: 'ms_bDestroying' is the flag of a phase, which is only ever set to true, so that what runs in the phase can rely on it staying set [malterlib-actor-phase]
  }
};
bool CApp::ms_bDestroying = false;
}

namespace NRound139 {
// Either value of a conditional that are both the own object, `(bChoose ?
// this : pOwn)`.
struct CApp : CActor {
  DMibActorPhase(Destroy) void f_DestroyPart() {}

  DMibActorPhase(Destroy) void f_Destroy(bool _bChoose) {
    auto *pOwn = this;
    (_bChoose ? this : pOwn)->f_DestroyPart();
  }

  CApp *m_pOther;
  DMibActorPhase(Destroy) void f_Other(bool _bChoose) {
    (_bChoose ? this : m_pOther)->f_DestroyPart();
// CHECK-MESSAGES: :[[@LINE-1]]:35: warning: 'f_DestroyPart' runs only in the Destroy phase of its actor, but is used here on another object, which may not be in that phase [malterlib-actor-phase]
  }
};
}

namespace NRound142 {
// An output of assembly writes the flag of a phase.
struct CAssembly : CActor {
  DMibActorPhaseFlag(StopApp) bool mp_bStopped = true;
  void f_Reset() { asm volatile("" : "=m"(mp_bStopped)); }
// CHECK-MESSAGES: :[[@LINE-1]]:43: warning: 'mp_bStopped' is the flag of a phase, which is only ever set to true, so that what runs in the phase can rely on it staying set [malterlib-actor-phase]
};

// A chain of conditionals of the same aliases of the own object, each told
// once.
struct CChain : CPhaseActor {
  DMibActorPhase(Destroy) void f_Clear() {}
  DMibActorPhase(Destroy) void f_Run(bool _bChoose) {
    auto *p0 = this;
    auto *p1 = _bChoose ? p0 : p0;
    auto *p2 = _bChoose ? p1 : p1;
    auto *p3 = _bChoose ? p2 : p2;
    auto *p4 = _bChoose ? p3 : p3;
    auto *p5 = _bChoose ? p4 : p4;
    auto *p6 = _bChoose ? p5 : p5;
    auto *p7 = _bChoose ? p6 : p6;
    auto *p8 = _bChoose ? p7 : p7;
    auto *p9 = _bChoose ? p8 : p8;
    auto *p10 = _bChoose ? p9 : p9;
    auto *p11 = _bChoose ? p10 : p10;
    auto *p12 = _bChoose ? p11 : p11;
    auto *p13 = _bChoose ? p12 : p12;
    auto *p14 = _bChoose ? p13 : p13;
    auto *p15 = _bChoose ? p14 : p14;
    auto *p16 = _bChoose ? p15 : p15;
    auto *p17 = _bChoose ? p16 : p16;
    auto *p18 = _bChoose ? p17 : p17;
    auto *p19 = _bChoose ? p18 : p18;
    auto *p20 = _bChoose ? p19 : p19;
    auto *p21 = _bChoose ? p20 : p20;
    auto *p22 = _bChoose ? p21 : p21;
    auto *p23 = _bChoose ? p22 : p22;
    auto *p24 = _bChoose ? p23 : p23;
    auto *p25 = _bChoose ? p24 : p24;
    auto *p26 = _bChoose ? p25 : p25;
    auto *p27 = _bChoose ? p26 : p26;
    auto *p28 = _bChoose ? p27 : p27;
    auto *p29 = _bChoose ? p28 : p28;
    auto *p30 = _bChoose ? p29 : p29;
    auto *p31 = _bChoose ? p30 : p30;
    auto *p32 = _bChoose ? p31 : p31;
    auto *p33 = _bChoose ? p32 : p32;
    auto *p34 = _bChoose ? p33 : p33;
    auto *p35 = _bChoose ? p34 : p34;
    auto *p36 = _bChoose ? p35 : p35;
    auto *p37 = _bChoose ? p36 : p36;
    auto *p38 = _bChoose ? p37 : p37;
    auto *p39 = _bChoose ? p38 : p38;
    auto *p40 = _bChoose ? p39 : p39;
    p40->f_Clear();
  }
};

// And of the same aliases of a member that is moved, also told once.
struct CState {
  DMibActorEntries(Destroy) TCMap<int, int> mp_Entries;
};

struct CMove : CActor {
  CState mp_State;
  void f_Run(bool _bChoose) {
    auto &r0 = mp_State;
    auto &r1 = _bChoose ? r0 : r0;
    auto &r2 = _bChoose ? r1 : r1;
    auto &r3 = _bChoose ? r2 : r2;
    auto &r4 = _bChoose ? r3 : r3;
    auto &r5 = _bChoose ? r4 : r4;
    auto &r6 = _bChoose ? r5 : r5;
    auto &r7 = _bChoose ? r6 : r6;
    auto &r8 = _bChoose ? r7 : r7;
    auto &r9 = _bChoose ? r8 : r8;
    auto &r10 = _bChoose ? r9 : r9;
    auto &r11 = _bChoose ? r10 : r10;
    auto &r12 = _bChoose ? r11 : r11;
    auto &r13 = _bChoose ? r12 : r12;
    auto &r14 = _bChoose ? r13 : r13;
    auto &r15 = _bChoose ? r14 : r14;
    auto &r16 = _bChoose ? r15 : r15;
    auto &r17 = _bChoose ? r16 : r16;
    auto &r18 = _bChoose ? r17 : r17;
    auto &r19 = _bChoose ? r18 : r18;
    auto &r20 = _bChoose ? r19 : r19;
    auto &r21 = _bChoose ? r20 : r20;
    auto &r22 = _bChoose ? r21 : r21;
    auto &r23 = _bChoose ? r22 : r22;
    auto &r24 = _bChoose ? r23 : r23;
    auto &r25 = _bChoose ? r24 : r24;
    auto &r26 = _bChoose ? r25 : r25;
    auto &r27 = _bChoose ? r26 : r26;
    auto &r28 = _bChoose ? r27 : r27;
    auto &r29 = _bChoose ? r28 : r28;
    auto &r30 = _bChoose ? r29 : r29;
    auto &r31 = _bChoose ? r30 : r30;
    auto &r32 = _bChoose ? r31 : r31;
    auto &r33 = _bChoose ? r32 : r32;
    auto &r34 = _bChoose ? r33 : r33;
    auto &r35 = _bChoose ? r34 : r34;
    auto &r36 = _bChoose ? r35 : r35;
    auto &r37 = _bChoose ? r36 : r36;
    auto &r38 = _bChoose ? r37 : r37;
    auto &r39 = _bChoose ? r38 : r38;
    auto &r40 = _bChoose ? r39 : r39;
    CState Moved = fg_Move(r40);
// CHECK-MESSAGES: :[[@LINE-1]]:20: warning: 'mp_Entries' is declared to lose entries only in Destroy, but is changed here in 'f_Run'; change it in a function of that phase (DMibActorPhase) [malterlib-actor-phase]
  }
};
}

namespace NRound148 {
// What a constant condition never runs.
struct CApp : CActor
{
	DMibActorEntries(StopApp) TCMap<int, int> m_Items;
	DMibActorPhaseFlag(StopApp) bool m_bStopped = false;
	DMibActorPhase(StopApp) void f_Stop()
	{
		m_Items.f_Clear();
	}

	void f_Run()
	{
		if (false)
			m_Items.f_Clear();
		if (false)
			m_bStopped = false;
		if (false)
			f_Stop();
		while (false)
			m_Items.f_Clear();
	}
};
}

namespace NRound149 {
struct CState
{
	DMibActorEntries(Destroy) TCMap<int, int> m_Items;
};

// An owner whose `operator->` is a template.
struct COwner
{
	CState *m_pState = nullptr;
	~COwner()
	{
		delete m_pState;
	}
	template <typename t_CType = void>
	CState *operator->() const
	{
		return m_pState;
	}
	void f_Reset()
	{
		delete m_pState;
		m_pState = nullptr;
	}
};

struct CApp : CActor
{
	COwner m_Owner;
	void f_Erase()
	{
		(void)m_Owner->m_Items;
		m_Owner.f_Reset();
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: 'm_Items' is declared to lose entries only in Destroy, but is changed here in 'f_Erase'; change it in a function of that phase (DMibActorPhase) [malterlib-actor-phase]
	}
};

// An observer whose destructor does nothing doesn't own.
struct CFlagged
{
	DMibActorPhaseFlag(Destroy) bool m_bGone = false;
};

struct CObserver
{
	CFlagged *m_pFlagged = nullptr;
	~CObserver() {}
	CFlagged *operator->() const
	{
		return m_pFlagged;
	}
};

struct CObserving : CActor
{
	CObserver m_Observer;
	void f_Forget()
	{
		m_Observer = CObserver{};
	}
};
}

namespace NRound150 {
struct CState
{
	DMibActorEntries(Destroy) TCMap<int, int> m_Items;
};

// A local copy cleared through a lambda that captures it.
struct CApp : CActor
{
	CState m_State;
	void f_ClearCopy()
	{
		auto Copy = m_State;
		[&Copy] { Copy.m_Items.f_Clear(); }();
	}
};

// Classes that hold many members of the same class are each looked at once.
struct CC0
{
	~CC0() {}
};
#define DRound150Level(N, P) \
	struct N \
	{ \
		P m_A, m_B, m_C, m_D, m_E, m_F, m_G, m_H; \
		~N() {} \
		N &operator=(N const &_Other) { return *this; } \
	};
DRound150Level(CC1, CC0)
DRound150Level(CC2, CC1)
DRound150Level(CC3, CC2)
DRound150Level(CC4, CC3)
DRound150Level(CC5, CC4)
DRound150Level(CC6, CC5)
DRound150Level(CC7, CC6)
DRound150Level(CC8, CC7)
DRound150Level(CC9, CC8)
DRound150Level(CC10, CC9)
DRound150Level(CC11, CC10)
DRound150Level(CC12, CC11)

void fg_Assign(CC12 &_Target, CC12 const &_Source)
{
	_Target = _Source;
}
}

namespace NRound151 {
TCFuture<int> fg_Suspend();

// A coroutine lambda called by the constructor goes on after it.
struct CApp : CActor
{
	DMibActorEntries() TCMap<int, int> mp_Items;

	CApp()
	{
		[](CApp *_pThis) -> TCFuture<int>
			{
				co_await fg_Suspend();
				_pThis->mp_Items.f_Clear();
// CHECK-MESSAGES: :[[@LINE-1]]:5: warning: 'mp_Items' is declared to never lose entries, but is changed here in 'CApp'; remove its entries only in its constructor or destructor [malterlib-actor-phase]
				co_return 0;
			}
		(this);
	}
};

// A local lambda that is only called, with many uses in it.
#define DRound151R2(x) x x
#define DRound151R8(x) DRound151R2(DRound151R2(DRound151R2(x)))
#define DRound151R64(x) DRound151R8(DRound151R8(x))
#define DRound151R4096(x) DRound151R64(DRound151R64(x))
struct CMany : CActor
{
	DMibActorEntries() TCMap<int, int> mp_Items;
	CMany()
	{
		auto fInitialize = [this]
			{
				DRound151R4096(DRound151R2(mp_Items.f_Clear();))
			}
		;
		fInitialize();
	}
};
}

namespace NRound153 {
// A call that can clear the flag before it is assigned what the call gives.
struct CApp : CActor {
  DMibActorPhaseFlag(StopApp) bool m_bStopped = false;
  bool f_Start();
  void f_Test() { m_bStopped = m_bStopped || f_Start(); }
// CHECK-MESSAGES: :[[@LINE-1]]:30: warning: 'm_bStopped' is the flag of a phase, which is only ever set to true, so that what runs in the phase can rely on it staying set [malterlib-actor-phase]
};

// Elements that differ by the side effects of the subscripts.
struct CState {
  DMibActorPhaseFlag(Destroy) bool m_bDestroyed = false;
};
struct CStates : CActor {
  CState m_States[2];
  void f_Test() {
    m_States[1].m_bDestroyed = true;
    int iState = 0;
    m_States[iState++].m_bDestroyed = m_States[iState++].m_bDestroyed;
// CHECK-MESSAGES: :[[@LINE-1]]:37: warning: 'm_bDestroyed' is the flag of a phase, which is only ever set to true, so that what runs in the phase can rely on it staying set [malterlib-actor-phase]
  }
};
}

namespace NRound154 {
// A constructor in what is or-ed, which can set the flag before it is
// written back.
struct CApp;
struct CSet {
  CSet(CApp *_pApp);
  operator bool() const { return false; }
};
struct CApp : CActor {
  DMibActorPhaseFlag(StopApp) bool m_bStopped = false;
  void f_Test() { m_bStopped = m_bStopped || CSet{this}; }
// CHECK-MESSAGES: :[[@LINE-1]]:30: warning: 'm_bStopped' is the flag of a phase, which is only ever set to true, so that what runs in the phase can rely on it staying set [malterlib-actor-phase]
};
CSet::CSet(CApp *_pApp) { _pApp->m_bStopped = true; }

// The target reached again after the right side, which changes the index.
struct CState {
  DMibActorPhaseFlag(StopApp) bool m_bStopped = false;
};
struct CStates : CActor {
  CState m_States[2];
  void f_Test() {
    m_States[1].m_bStopped = true;
    int iState = 0;
    m_States[iState].m_bStopped = m_States[iState].m_bStopped || (++iState, false);
// CHECK-MESSAGES: :[[@LINE-1]]:33: warning: 'm_bStopped' is the flag of a phase, which is only ever set to true, so that what runs in the phase can rely on it staying set [malterlib-actor-phase]
  }
  // A constant index, and one that doesn't change, are the same object.
  void f_Same(int _iState) {
    m_States[0].m_bStopped = m_States[0].m_bStopped || _iState > 0;
    m_States[_iState].m_bStopped = m_States[_iState].m_bStopped || _iState > 0;
  }
};
}

namespace NRound155 {
struct CApp : CActor {
  DMibActorPhaseFlag(StopApp) bool m_bStopped = false;
  DMibActorEntries(Destroy) TCMap<int, int> m_Items;
  // Through a cast that adds const before another removes it.
  void f_Reset() {
    *const_cast<bool *>(static_cast<bool const *>(&m_bStopped)) = false;
// CHECK-MESSAGES: :[[@LINE-1]]:51: warning: 'm_bStopped' is the flag of a phase, which is only ever set to true, so that what runs in the phase can rely on it staying set [malterlib-actor-phase]
    using CItems = decltype(m_Items);
    const_cast<CItems *>(static_cast<CItems const *>(&m_Items))->f_Clear();
// CHECK-MESSAGES: :[[@LINE-1]]:54: warning: 'm_Items' is declared to lose entries only in Destroy, but is changed here in 'f_Reset'; change it in a function of that phase (DMibActorPhase) [malterlib-actor-phase]
  }
  // Through many parentheses.
  void f_Parenthesized() {
    bool const *pStopped = &m_bStopped;
    *const_cast<bool *>(((((((((pStopped))))))))) = false;
// CHECK-MESSAGES: :[[@LINE-1]]:51: warning: 'm_bStopped' is the flag of a phase, which is only ever set to true, so that what runs in the phase can rely on it staying set [malterlib-actor-phase]
  }
};
}

namespace NRound160 {
// The object of another actor through an element and an offset.
struct CState {
  DMibActorEntries(Destroy) TCMap<int, int> m_Items;
};
DMibActorPhase(Destroy) void fg_Clear(CState *_pThis) { _pThis->m_Items.f_Clear(); }
struct CApp : CActor {
  CState m_States[2];
  CState m_State;
  DMibActorPhase(Destroy) void f_Destroy(CApp *_pOther) {
    fg_Clear(&_pOther->m_States[0]);
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: 'fg_Clear' runs only in the Destroy phase of its actor, but is given another object here, which may not be in that phase [malterlib-actor-phase]
    fg_Clear(&_pOther->m_State + 0);
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: 'fg_Clear' runs only in the Destroy phase of its actor, but is given another object here, which may not be in that phase [malterlib-actor-phase]
  }
};
}

namespace NRound163 {
// An operator that adds entries, like `TCMap::operator+=`.
struct CAddOnly : TCMap<int, int> {
  CAddOnly &operator+=(CAddOnly const &_Other) {
    if (auto *pValue = _Other.f_FindEqual(1))
      (*this)[1] = *pValue;
    return *this;
  }
};
struct CApp : CActor {
  DMibActorEntries(Destroy) CAddOnly m_Items;
  void f_Add(CAddOnly const &_Other) { m_Items += _Other; }
};
}

namespace NRound166 {
// A function that only reads the flag can't set it.
struct CApp : CActor {
  DMibActorPhaseFlag(Destroy) bool mp_bDestroyed = false;
  bool m_bStopRequested = false;
  bool f_ShouldDestroy() const { return mp_bDestroyed || m_bStopRequested; }
  void f_Update() { mp_bDestroyed = mp_bDestroyed || f_ShouldDestroy(); }
};
}

namespace NRound169 {
// A member function called through a pointer to it can clear the flag.
struct CFlag {
  void store(bool _bValue);
};
struct CApp : CActor {
  DMibActorPhaseFlag(StopApp) CFlag mp_Flag;
  void f_Reset() { (mp_Flag.*&CFlag::store)(false); }
// CHECK-MESSAGES: :[[@LINE-1]]:28: warning: 'mp_Flag' is the flag of a phase, which is only ever set to true, so that what runs in the phase can rely on it staying set [malterlib-actor-phase]
};
// A reference bound through itself doesn't hang the check.
struct CSelf : CActor {
  CSelf &m_Self;
  DMibActorEntries(Owner) TCMap<CStr, int> mp_Items;
  void f_Run() {
    CSelf &Self = Self.m_Self;
    auto Key = fg_RandomID(Self.mp_Items);
    Self.mp_Items[Key] = 0;
    Self.mp_Items.f_Remove(Key);
// CHECK-MESSAGES: :[[@LINE-1]]:5: warning: 'mp_Items' is declared to lose entries only by the calls that added them, but is changed here in 'f_Run'; remove only the entry this call added, by its fresh key [malterlib-actor-phase]
  }
};
}

namespace NRound171 {
// Loaded atomically, or stored true, but not stored false.
struct CApp : CActor {
  DMibActorPhaseFlag(Destroy) bool m_bDestroyed = false;
  bool f_Read() { return __atomic_load_n(&m_bDestroyed, __ATOMIC_SEQ_CST); }
  void f_Set() { __atomic_store_n(&m_bDestroyed, true, __ATOMIC_SEQ_CST); }
  void f_Clear() { __atomic_store_n(&m_bDestroyed, false, __ATOMIC_SEQ_CST); }
// CHECK-MESSAGES: :[[@LINE-1]]:37: warning: 'm_bDestroyed' is the flag of a phase, which is only ever set to true, so that what runs in the phase can rely on it staying set [malterlib-actor-phase]
};
}

namespace NRound186 {
// Removing duplicates of a sorted vector removes entries.
template <typename t_CType>
struct TCVector {
  void f_SetLen(unsigned _Len);
  void f_UniqueIfSorted() { f_SetLen(1); }
};
struct CApp : CActor {
  DMibActorEntries(Destroy) TCVector<int> mp_Items;
  void f_Compact() {
    mp_Items.f_UniqueIfSorted();
// CHECK-MESSAGES: :[[@LINE-1]]:5: warning: 'mp_Items' is declared to lose entries only in Destroy, but is changed here in 'f_Compact'; change it in a function of that phase (DMibActorPhase) [malterlib-actor-phase]
  }
};
}

namespace NRound194 {
// Malterlib's atomics set a flag with `f_Store(true)`, `f_Exchange(true)` and
// `f_CompareExchangeStrong(Expected, true)`.
namespace NAtomic {
template <typename t_CType>
struct TCAtomic {
  void f_Store(t_CType _Value);
  t_CType f_Exchange(t_CType _Value);
  bool f_CompareExchangeStrong(t_CType &_Expected, t_CType _Desired);
};
}
struct CApp : CActor {
  DMibActorPhaseFlag(StopApp)
  NAtomic::TCAtomic<bool> m_bStopping;
  void f_Stop() {
    m_bStopping.f_Store(true);
    m_bStopping.f_Exchange(true);
    bool bExpected = false;
    m_bStopping.f_CompareExchangeStrong(bExpected, true);
    m_bStopping.f_Store(false);
// CHECK-MESSAGES: :[[@LINE-1]]:17: warning: 'm_bStopping' is the flag of a phase, which is only ever set to true, so that what runs in the phase can rely on it staying set [malterlib-actor-phase]
  }
};
}

namespace NRound198 {
// Assigning an object made with its flag set sets the flag.
struct CState {
  DMibActorPhaseFlag(StopApp) bool m_bStoppingApp = false;
  int m_Reason = 0;
};
struct CApp : CActor {
  CState mp_State;
  void f_Stop() { mp_State = CState{true, 3}; }
  void f_Reset() { mp_State = CState{false, 3}; }
// CHECK-MESSAGES: :[[@LINE-1]]:20: warning: assigning the object overwrites 'm_bStoppingApp', the flag of a phase, which is only ever set to true, so that what runs in the phase can rely on it staying set [malterlib-actor-phase]
};
}

namespace NRound199 {
// A member function of a local copy sets the flag of the copy.
struct CState {
  DMibActorPhaseFlag(StopApp) bool m_bStoppingApp = false;
  bool f_Stop() {
    m_bStoppingApp = true;
    return false;
  }
};
struct CApp : CActor {
  CState mp_State;
  void f_Run() {
    CState Other = mp_State;
    mp_State.m_bStoppingApp = mp_State.m_bStoppingApp || Other.f_Stop();
  }
};
}

namespace NRound201 {
// Each flag of a phase an object holds is overwritten by assigning it, unless
// what it is assigned sets it.
struct CApp : CActor {
  struct CState {
    DMibActorPhaseFlag(Destroy) bool m_bDestroyed = false;
    DMibActorPhaseFlag(StopApp) bool m_bStoppingApp = false;
  };
  CState mp_State;
  void f_Reset() { mp_State = CState{true, false}; }
// CHECK-MESSAGES: :[[@LINE-1]]:20: warning: assigning the object overwrites 'm_bStoppingApp', the flag of a phase, which is only ever set to true, so that what runs in the phase can rely on it staying set [malterlib-actor-phase]
  void f_Set() { mp_State = CState{true, true}; }
};
}

namespace NRound204 {
// Making a new one in place of what an optional holds destroys it first.
template <class t_CType>
struct TCOptional {
  t_CType m_Value;
  ~TCOptional();
  t_CType *operator->();
  t_CType &operator*();
  void f_Clear();
  t_CType &f_CreateNew();
};
struct CState {
  DMibActorPhaseFlag(StopApp) bool m_bStopping = false;
  DMibActorEntries(Destroy) TCMap<int, int> m_Items;
};
struct CApp : CActor {
  TCOptional<CState> mp_State;
  void f_Reset() {
    mp_State->m_bStopping = true;
    mp_State.f_CreateNew();
// CHECK-MESSAGES: :[[@LINE-1]]:5: warning: 'm_Items' is declared to lose entries only in Destroy, but is changed here in 'f_Reset'; change it in a function of that phase (DMibActorPhase) [malterlib-actor-phase]
// CHECK-MESSAGES: :[[@LINE-2]]:5: warning: assigning the object overwrites 'm_bStopping', the flag of a phase, which is only ever set to true, so that what runs in the phase can rely on it staying set [malterlib-actor-phase]
  }
};
}

namespace NRound205 {
// A member function that calls one of its object that removes entries.
template <class t_CType>
struct TCVector {
  void f_SetLen(unsigned _Len);
  t_CType *f_GetArray();
  t_CType *f_GetArray(unsigned _Len) {
    f_SetLen(_Len);
    return f_GetArray();
  }
};
struct CApp : CActor {
  DMibActorEntries(Destroy) TCVector<int> mp_Values;
  void f_Reset() { mp_Values.f_GetArray(0); }
// CHECK-MESSAGES: :[[@LINE-1]]:20: warning: 'mp_Values' is declared to lose entries only in Destroy, but is changed here in 'f_Reset'; change it in a function of that phase (DMibActorPhase) [malterlib-actor-phase]
  int *f_Read() { return mp_Values.f_GetArray(); }
};
}

namespace NRound218
{
	// The overridden function of a base made of an alias of many parts is
	// named by its template.
	using C0 = int;
#define DLink(d_Index, d_Previous) using C##d_Index = TCMap<C##d_Previous, C##d_Previous>;
	DLink(1, 0) DLink(2, 1) DLink(3, 2) DLink(4, 3) DLink(5, 4) DLink(6, 5)
	DLink(7, 6) DLink(8, 7) DLink(9, 8) DLink(10, 9) DLink(11, 10) DLink(12, 11)
	DLink(13, 12) DLink(14, 13) DLink(15, 14) DLink(16, 15) DLink(17, 16) DLink(18, 17)
	DLink(19, 18) DLink(20, 19) DLink(21, 20) DLink(22, 21) DLink(23, 22) DLink(24, 23)
#undef DLink
	template <typename t_CType>
	struct TCBaseActor : CActor
	{
		virtual void f_Stop();
	};
	struct CApp : TCBaseActor<C24>
	{
		DMibActorPhase(Destroy) void f_Stop() override;
// CHECK-MESSAGES: :[[@LINE-1]]:32: warning: 'f_Stop' runs only in Destroy, but overrides 'NRound218::TCBaseActor::f_Stop', which callers may call in any phase; declare 'NRound218::TCBaseActor::f_Stop' with DMibActorPhase of the same phases [malterlib-actor-phase]
	};
}

namespace NRound218Template
{
	// What an instantiation shares with its template, which doesn't depend on
	// its parameters, is checked in the instantiation.
	struct CApp : CActor
	{
		DMibActorEntries(Destroy) TCMap<int, int> mp_Items;
		DMibActorPhaseFlag(StopApp) bool m_bStopping = false;

		template <typename t_CKey>
		TCFuture<int> f_Reset(t_CKey _Key)
		{
			mp_Items.f_Remove(_Key);
// CHECK-MESSAGES: :[[@LINE-1]]:4: warning: 'mp_Items' is declared to lose entries only in Destroy, but is changed here in 'f_Reset'; change it in a function of that phase (DMibActorPhase) [malterlib-actor-phase]
			m_bStopping = false;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'm_bStopping' is the flag of a phase, which is only ever set to true, so that what runs in the phase can rely on it staying set [malterlib-actor-phase]
			co_return 0;
		}

		// Not one nothing instantiates.
		template <typename t_CKey>
		TCFuture<int> f_Unused(t_CKey _Key)
		{
			mp_Items.f_Remove(_Key);
			co_return 0;
		}

		void f_Test()
		{
			f_Reset(1);
		}
	};
}

namespace NRound226
{
	// A flag set to true in what a nested aggregate makes is set, also in a
	// base, an element of an array and after an unnamed bit-field, but not
	// when one of its copies isn't.
	struct CPhaseState
	{
		DMibActorPhaseFlag(StopApp) bool m_bStopped = false;
	};

	struct CState
	{
		CPhaseState m_Phase;
	};

	struct CTwice
	{
		CPhaseState m_First;
		CPhaseState m_Second;
	};

	struct CDerived : CPhaseState
	{
		int m_Value = 0;
	};

	struct CArray
	{
		CPhaseState m_Phases[2];
	};

	struct CBits
	{
		int : 4;
		DMibActorPhaseFlag(StopApp) bool m_bStopped = false;
	};

	struct CApp : CActor
	{
		CState m_State;
		CTwice m_Twice;
		CDerived m_Derived;
		CArray m_Array;
		CBits m_Bits;

		void f_Stop()
		{
			m_State = CState{{true}};
			m_Twice = CTwice{{true}, {true}};
			m_Derived = CDerived{{true}, 3};
			m_Array = CArray{{{true}, {true}}};
			m_Bits = CBits{true};
		}

		void f_Reset()
		{
			m_State = CState{{false}};
// CHECK-MESSAGES: :[[@LINE-1]]:4: warning: assigning the object overwrites 'm_bStopped', the flag of a phase, which is only ever set to true, so that what runs in the phase can rely on it staying set [malterlib-actor-phase]
			m_State = CState{};
// CHECK-MESSAGES: :[[@LINE-1]]:4: warning: assigning the object overwrites 'm_bStopped', the flag of a phase, which is only ever set to true, so that what runs in the phase can rely on it staying set [malterlib-actor-phase]
			m_Twice = CTwice{{true}, {false}};
// CHECK-MESSAGES: :[[@LINE-1]]:4: warning: assigning the object overwrites 'm_bStopped', the flag of a phase, which is only ever set to true, so that what runs in the phase can rely on it staying set [malterlib-actor-phase]
			m_Twice = CTwice{{true}};
// CHECK-MESSAGES: :[[@LINE-1]]:4: warning: assigning the object overwrites 'm_bStopped', the flag of a phase, which is only ever set to true, so that what runs in the phase can rely on it staying set [malterlib-actor-phase]
			m_Derived = CDerived{{false}, 3};
// CHECK-MESSAGES: :[[@LINE-1]]:4: warning: assigning the object overwrites 'm_bStopped', the flag of a phase, which is only ever set to true, so that what runs in the phase can rely on it staying set [malterlib-actor-phase]
			m_Array = CArray{{{true}}};
// CHECK-MESSAGES: :[[@LINE-1]]:4: warning: assigning the object overwrites 'm_bStopped', the flag of a phase, which is only ever set to true, so that what runs in the phase can rely on it staying set [malterlib-actor-phase]
			m_Bits = CBits{false};
// CHECK-MESSAGES: :[[@LINE-1]]:4: warning: assigning the object overwrites 'm_bStopped', the flag of a phase, which is only ever set to true, so that what runs in the phase can rely on it staying set [malterlib-actor-phase]
		}
	};
}

namespace NRound229
{
	// The cleanup of a local scope guard in a constructor runs in it, unlike a
	// shared one, which can outlive it.
	struct CApp : CActor
	{
		DMibActorEntries() TCMap<int, int> mp_Items;

		CApp()
		{
			auto Cleanup = g_OnScopeExit / [this]
				{
					mp_Items.f_Clear();
				}
			;
			mp_Items[1] = 1;
		}

		CApp(int _Value)
		{
			auto Cleanup = g_OnScopeExitShared / [this]
				{
					mp_Items.f_Clear();
// CHECK-MESSAGES: :[[@LINE-1]]:6: warning: 'mp_Items' is declared to never lose entries, but is changed here in 'CApp'; remove its entries only in its constructor or destructor [malterlib-actor-phase]
				}
			;
			mp_Items[1] = _Value;
		}
	};
}

namespace NRound236
{
	// A helper whose clearing never runs.
	struct CItems : TCVector<int>
	{
		int *f_GetArray()
		{
			if constexpr (false)
				f_Clear();
			return nullptr;
		}
		int *f_GetNever()
		{
			if (false)
				f_Clear();
			return nullptr;
		}
	};
	struct CApp : CActor
	{
		DMibActorEntries(Destroy) CItems mp_Items;
		DMibActorEntries(Destroy) TCMap<int, int> mp_Values;
		CApp *m_pOther;

		void f_Run()
		{
			(void)mp_Items.f_GetArray();
			(void)mp_Items.f_GetNever();
		}
		// The object a constant condition chooses.
		template <bool t_bUseOwn>
		DMibActorPhase(Destroy) void f_Clear()
		{
			(t_bUseOwn ? this : m_pOther)->mp_Values.f_Clear();
// CHECK-MESSAGES: :[[@LINE-1]]:4: warning: 'mp_Values' is declared to lose entries only in Destroy, but is changed here in 'f_Clear'; change it in a function of that phase (DMibActorPhase) [malterlib-actor-phase]
		}
		DMibActorPhase(Destroy) void f_Destroy()
		{
			f_Clear<true>();
			f_Clear<false>();
		}
	};
}

// What a member of the entries calls of its own object is looked into once at
// each depth, not once for each call of it.
#define DRound339Four(X) X X X X
#define DRound339Sixteen(X) DRound339Four(DRound339Four(X))
#define DRound339SixtyFour(X) DRound339Four(DRound339Sixteen(X))
struct CRound339Entries
{
	void f_Base() {}
	void f_Level1() { DRound339SixtyFour(f_Base();) }
	void f_Level2() { DRound339SixtyFour(f_Level1();) }
	void f_Level3() { DRound339SixtyFour(f_Level2();) }
	void f_Level4() { DRound339SixtyFour(f_Level3();) }
};
struct CRound339App : CPhaseActor
{
	DMibActorEntries(Destroy) CRound339Entries mp_Items;
	void f_Test() { mp_Items.f_Level4(); }
};
