// RUN: %check_clang_tidy -std=c++20-or-later %s malterlib-coroutine-actor-reference %t -- -config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" -- -I %S/Inputs

#include "malterlib-actor-phase.h"

struct CDownload
{
	int m_Value = 0;
};

TCFuture<int> fg_Suspend();

struct CApp : CDistributedAppActor
{
	// The entry this call added by its fresh key is only removed by this call,
	// or when the app stops or the actor is destroyed, which the guard ends the
	// coroutine for.
	TCFuture<int> f_Guarded()
	{
		auto OnResume = co_await fp_CheckStoppedOrDestroyedOnResume();
		auto DownloadID = fg_RandomID(mp_Downloads);
		auto &Download = mp_Downloads[DownloadID];
		co_await fg_Suspend();
		Download.m_Value = 1;
		auto *pDownload = mp_Downloads.f_FindEqual(DownloadID);
		co_await fg_Suspend();
		co_return pDownload->m_Value;
	}

	// The address of a reference to the entry, and a key passed by a forwarding
	// reference, keep the entry this call's own.
	TCFuture<int> f_GuardedAlias()
	{
		auto OnResume = co_await fp_CheckStoppedOrDestroyedOnResume();
		auto DownloadID = fg_RandomID(mp_Downloads);
		auto &Download = mp_Downloads[DownloadID];
		auto *pDownload = &Download;
		co_await fg_Suspend();
		int Value = pDownload->m_Value + Download.m_Value;
		mp_Downloads.f_RemoveKey(DownloadID);
		co_return Value;
	}

	// A guard of another actor doesn't guard this one, and a callback that
	// returns success in the branch of the flag doesn't guard.
	TCFuture<int> f_OtherGuard(CApp *_pOther)
	{
		auto OnResume = co_await _pOther->fp_CheckStoppedOrDestroyedOnResume();
		auto *pEntry = mp_Insertions.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'pEntry' refers into the data of the actor and is used after a suspension; 'mp_Insertions' {{.*}}
	}

	TCFuture<int> f_EarlySuccess(bool _bSkip)
	{
		auto OnResume = co_await fg_OnResume
			(
				[this, _bSkip]() -> CExceptionPointer
				{
					if (mp_State.m_bStoppingApp || f_IsDestroyed())
					{
						if (_bSkip)
							return {};
						return fg_MakeError("Shutting down");
					}
					return {};
				}
			)
		;
		auto *pEntry = mp_Insertions.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'pEntry' refers into the data of the actor and is used after a suspension; 'mp_Insertions' {{.*}}
	}

	// A guard of a phase the entries aren't removed in doesn't help.
	TCFuture<int> f_DestroyGuardOnly()
	{
		auto OnResume = co_await f_CheckDestroyedOnResume();
		auto DownloadID = fg_RandomID(mp_Downloads);
		auto &Download = mp_Downloads[DownloadID];
		co_await fg_Suspend();
		co_return Download.m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'Download' refers into the data of the actor and is used after a suspension; 'mp_Downloads' loses entries only in Destroy and StopApp, so a guard of those phases that is alive at the use, like fp_CheckStoppedOrDestroyedOnResume, makes it safe [malterlib-coroutine-actor-reference]
	}

	// An entry another call added can be removed by that call.
	TCFuture<int> f_NotOwned(CStr _Key)
	{
		auto OnResume = co_await fp_CheckStoppedOrDestroyedOnResume();
		auto &Download = mp_Downloads[_Key];
		co_await fg_Suspend();
		co_return Download.m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'Download' refers into the data of the actor and is used after a suspension, during which {{.*}}
	}

	// A guard that has ended doesn't guard.
	TCFuture<int> f_GuardEnded()
	{
		auto DownloadID = fg_RandomID(mp_Downloads);
		auto &Download = mp_Downloads[DownloadID];
		{
			auto OnResume = co_await fp_CheckStoppedOrDestroyedOnResume();
			co_await fg_Suspend();
			Download.m_Value = 1;
		}
		co_await fg_Suspend();
		co_return Download.m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'Download' refers into the data of the actor and is used after a suspension; 'mp_Downloads' {{.*}}
	}

	// A callback that ends the coroutine when the flags are set guards their
	// phases.
	TCFuture<int> f_ManualGuard(CStr _Key)
	{
		auto OnResume = co_await fg_OnResume
			(
				[this]() -> CExceptionPointer
				{
					if (mp_State.m_bStoppingApp || f_IsDestroyed())
						return fg_MakeError("Shutting down");
					return {};
				}
			)
		;
		auto *pEntry = mp_Insertions.f_FindEqual(_Key);
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	// One that tests only some of the flags guards only their phases, and one
	// that returns success before it tests them guards nothing.
	TCFuture<int> f_PartialGuard(CStr _Key)
	{
		auto OnResume = co_await fg_OnResume
			(
				[this]() -> CExceptionPointer
				{
					if (f_IsDestroyed())
						return fg_MakeError("Destroyed");
					return {};
				}
			)
		;
		auto OnResumeLate = co_await fg_OnResume
			(
				[this]() -> CExceptionPointer
				{
					if (mp_bReady)
						return {};
					if (mp_State.m_bStoppingApp)
						return fg_MakeError("Stopping");
					return {};
				}
			)
		;
		auto *pEntry = mp_Insertions.f_FindEqual(_Key);
		co_await fg_Suspend();
		co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'pEntry' refers into the data of the actor and is used after a suspension; 'mp_Insertions' loses entries only in Destroy and StopApp, {{.*}}
	}

	// A collection that never loses entries needs no guard, unless adding
	// entries moves the others.
	TCFuture<int> f_InsertOnly(CStr _Key, int _Index)
	{
		auto *pEntry = mp_Forever.f_FindEqual(_Key);
		auto &Element = mp_Elements[_Index];
		co_await fg_Suspend();
		co_return pEntry->m_Value + Element.m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:31: warning: 'Element' refers into the data of the actor and is used after a suspension, during which {{.*}}
	}

	// A loop over a collection guarded for its phases, which no call removes
	// its own entries from.
	TCFuture<int> f_Loop()
	{
		auto OnResume = co_await fp_CheckStoppedOrDestroyedOnResume();
		int Sum = 0;
		for (auto &Entry : mp_Insertions)
		{
			co_await fg_Suspend();
			Sum += Entry.m_Value;
		}
		for (auto &Entry : mp_Downloads)
// CHECK-MESSAGES: :[[@LINE-1]]:22: warning: the loop goes on over the data of the actor after a suspension, {{.*}}
		{
			co_await fg_Suspend();
			Sum += Entry.m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:11: warning: 'Entry' refers into the data of the actor and is used after a suspension, during which {{.*}}
		}
		co_return Sum;
	}

	// A guard of the current actor protects the entries looked up through
	// any parameter given its object, a callback that may return success,
	// also through a conditional or a jump, doesn't guard, and a loop over a
	// collection through free begin and end goes over the data.
	static TCFuture<int> fs_Round22(CApp *_pThis, CApp *_pOther)
	{
		auto OnResume = co_await _pThis->fp_CheckStoppedOrDestroyedOnResume();
		auto *pOther = _pOther->mp_Insertions.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pOther->m_Value;
	}

	TCFuture<int> f_ConditionalResult(bool _bSkip)
	{
		auto OnResume = co_await fg_OnResume
			(
				[this, _bSkip]() -> CExceptionPointer
				{
					if (mp_State.m_bStoppingApp || f_IsDestroyed())
						return _bSkip ? CExceptionPointer{} : fg_MakeError("Stopped");
					return {};
				}
			)
		;
		auto *pEntry = mp_Insertions.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'pEntry' refers into the data of the actor and is used after a suspension; 'mp_Insertions' {{.*}}
	}

	TCFuture<int> f_Jump(bool _bSkip)
	{
		auto OnResume = co_await fg_OnResume
			(
				[this, _bSkip]() -> CExceptionPointer
				{
					if (_bSkip)
						goto Success;
					if (mp_State.m_bStoppingApp || f_IsDestroyed())
						return fg_MakeError("Stopped");
				Success:
					return {};
				}
			)
		;
		auto *pEntry = mp_Insertions.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'pEntry' refers into the data of the actor and is used after a suspension; 'mp_Insertions' {{.*}}
	}

	TCFuture<int> f_FreeLoop()
	{
		int Sum = 0;
		for (auto &Entry : mp_FreeList)
// CHECK-MESSAGES: :[[@LINE-1]]:22: warning: the loop goes on over the data of the actor after a suspension, {{.*}}
		{
			co_await fg_Suspend();
			Sum += Entry.m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:11: warning: 'Entry' refers into the data of the actor and is used after a suspension, {{.*}}
		}
		co_return Sum;
	}

	// A named result may be success, and guards alive together cover their
	// phases together.
	DMibActorPhaseGuard(StopApp) TCFuture<COnResumeScope> f_StopGuard();

	TCFuture<int> f_NamedResult()
	{
		auto OnResume = co_await fg_OnResume
			(
				[this]() -> CExceptionPointer
				{
					CExceptionPointer Error;
					if (mp_State.m_bStoppingApp || f_IsDestroyed())
						return Error;
					return {};
				}
			)
		;
		auto *pEntry = mp_Insertions.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'pEntry' refers into the data of the actor and is used after a suspension; 'mp_Insertions' {{.*}}
	}

	TCFuture<int> f_CombinedGuards()
	{
		auto DestroyGuard = co_await f_CheckDestroyedOnResume();
		auto StopGuard = co_await f_StopGuard();
		auto *pEntry = mp_Insertions.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	// Round 24: a null pointer is success, and a guard moved away doesn't
	// guard.
	TCFuture<int> f_NullResult()
	{
		auto OnResume = co_await fg_OnResume
			(
				[this]() -> CExceptionPointer
				{
					if (mp_State.m_bStoppingApp || f_IsDestroyed())
						return CExceptionPointer(nullptr);
					return {};
				}
			)
		;
		auto *pEntry = mp_Insertions.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'pEntry' refers into the data of the actor and is used after a suspension; 'mp_Insertions' {{.*}}
	}

	TCFuture<int> f_MovedGuard()
	{
		auto OnResume = co_await fp_CheckStoppedOrDestroyedOnResume();
		{
			auto Moved = fg_Move(OnResume);
		}
		auto *pEntry = mp_Insertions.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'pEntry' refers into the data of the actor and is used after a suspension; 'mp_Insertions' {{.*}}
	}

	// Round 25: a function that may return success, and a key fresh in another
	// collection.
	TCFuture<int> f_SuccessCall()
	{
		auto OnResume = co_await fg_OnResume
			(
				[this]() -> CExceptionPointer
				{
					if (mp_State.m_bStoppingApp || f_IsDestroyed())
						return fs_Success();
					return {};
				}
			)
		;
		auto *pEntry = mp_Insertions.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'pEntry' refers into the data of the actor and is used after a suspension; 'mp_Insertions' {{.*}}
	}

	TCFuture<int> f_FreshElsewhere()
	{
		auto OnResume = co_await fp_CheckStoppedOrDestroyedOnResume();
		auto DownloadID = fg_RandomID(mp_Insertions);
		auto &Download = mp_Downloads[DownloadID];
		co_await fg_Suspend();
		co_return Download.m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'Download' refers into the data of the actor and is used after a suspension, during which {{.*}}
	}

	static CExceptionPointer fs_Success();

	// A pointer to an annotated collection keeps its annotation.
	TCFuture<int> f_CollectionPointer()
	{
		auto OnResume = co_await fp_CheckStoppedOrDestroyedOnResume();
		auto *pInsertions = &mp_Insertions;
		auto *pEntry = pInsertions->f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	// Round 26: a guarded loop through free begin and end over an annotated
	// collection.
	TCFuture<int> f_GuardedFreeLoop()
	{
		auto OnResume = co_await f_CheckDestroyedOnResume();
		int Sum = 0;
		for (auto &Entry : mp_GuardedList)
		{
			co_await fg_Suspend();
			Sum += Entry.m_Value;
		}
		co_return Sum;
	}

	DMibActorEntries(Destroy) TCLinkedList<CDownload> mp_GuardedList;

	TCFreeList<CDownload> mp_FreeList;
	DMibActorEntries(Destroy, StopApp, Owner) TCMap<CStr, CDownload> mp_Downloads;
	DMibActorEntries(Destroy, StopApp) TCMap<CStr, CDownload> mp_Insertions;
	DMibActorEntries() TCMap<CStr, CDownload> mp_Forever;
	DMibActorEntries() TCVector<CDownload> mp_Elements;
	bool mp_bReady = false;
};

struct CEntryHandle
{
	CDownload &m_Entry;
};

struct CRound28 : CPhaseActor
{
	// An aggregate handle made from a lookup is guarded like the lookup.
	TCFuture<int> f_AggregateHandle()
	{
		auto Guard = co_await f_CheckDestroyedOnResume();
		CEntryHandle Handle{*mp_Items.f_FindEqual("Key")};
		co_await fg_Suspend();
		co_return Handle.m_Entry.m_Value;
	}

	// A callback that only tests a pointer for null doesn't re-validate what
	// is made from it: the entry it points to can be gone.
	TCFuture<int> f_NullTest()
	{
		auto *pEntry = mp_Plain.f_FindEqual("Key");
		CDownload &Entry = *pEntry;
		auto Guard = co_await fg_OnResume
			(
				[&]() -> CExceptionPointer
				{
					if (!pEntry)
						return fg_MakeError("Missing");
					return {};
				}
			)
		;
		co_await fg_Suspend();
		co_return Entry.m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: {{.*}}
	}

	DMibActorEntries(Destroy) TCMap<CStr, CDownload> mp_Items;
	TCMap<CStr, CDownload> mp_Plain;
};

struct CRound29 : CPhaseActor
{
	// An entry the call removed itself is gone, also when only it removes
	// entries; a removal after the last use, or in the cleanup, isn't.
	TCFuture<int> f_RemovedOwn()
	{
		auto ID = fg_RandomID(mp_Owned);
		auto &Entry = mp_Owned[ID];
		mp_Owned.f_Remove(ID);
		co_await fg_Suspend();
		co_return Entry.m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: {{.*}}
	}

	TCFuture<int> f_RemovedOwnInCleanup()
	{
		auto ID = fg_RandomID(mp_Owned);
		auto Cleanup = co_await fg_AsyncDestroy
			(
				[this, ID]() -> TCFuture<int>
				{
					mp_Owned.f_Remove(ID);
					co_return 0;
				}
			)
		;
		auto &Entry = mp_Owned[ID];
		co_await fg_Suspend();
		co_return Entry.m_Value;
	}

	TCFuture<int> f_RemovedOwnAtScopeExit()
	{
		auto ID = fg_RandomID(mp_Owned);
		auto &Entry = mp_Owned[ID];
		auto Cleanup = g_OnScopeExit / [this, ID]
			{
				mp_Owned.f_Remove(ID);
			}
		;
		co_await fg_Suspend();
		co_return Entry.m_Value;
	}

	TCFuture<int> f_RemovedOwnAtInnerScopeExit()
	{
		auto ID = fg_RandomID(mp_Owned);
		auto &Entry = mp_Owned[ID];
		{
			auto Cleanup = g_OnScopeExit / [this, ID]
				{
					mp_Owned.f_Remove(ID);
				}
			;
		}
		co_await fg_Suspend();
		co_return Entry.m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: {{.*}}
	}

	DMibActorEntries(Owner) TCMap<CStr, CDownload> mp_Owned;
};

struct CRound29Actor : CActor
{
	TCMap<CStr, CDownload> mp_Items;
};

// A lookup captured through a local keeps the actor it was made from.
TCFuture<int> fg_CapturedLookup(CRound29Actor *_pActor)
{
	auto *pEntry = _pActor->mp_Items.f_FindEqual("Key");
	co_return co_await fg_CallSafe
		(
			[pEntry]() -> TCFuture<int>
			{
				co_await fg_Suspend();
				co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: {{.*}}
			}
		)
	;
}

struct CRound30 : CDistributedAppActor
{
	// A lambda that got the key doesn't own the entry: the call that added it
	// can remove it while the lambda is suspended.
	TCFuture<int> f_ChildCoroutine()
	{
		auto ID = fg_RandomID(mp_Owned);
		auto &Entry = mp_Owned[ID];
		auto Cleanup = g_OnScopeExit / [this, ID]
			{
				mp_Owned.f_Remove(ID);
			}
		;
		co_await fg_CallSafe
			(
				[this, ID]() -> TCFuture<int>
				{
					auto *pEntry = mp_Owned.f_FindEqual(ID);
					co_await fg_Suspend();
					co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: {{.*}}
				}
			)
		;
		co_await fg_Suspend();
		co_return Entry.m_Value;
	}

	// Removed through an alias of the collection.
	TCFuture<int> f_RemovedThroughAlias()
	{
		auto ID = fg_RandomID(mp_Owned);
		auto &Entry = mp_Owned[ID];
		auto &Owned = mp_Owned;
		Owned.f_Remove(ID);
		co_await fg_Suspend();
		co_return Entry.m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: {{.*}}
	}

	// A cleanup that isn't held runs at the end of its full-expression, and
	// one awaited early runs there.
	TCFuture<int> f_DiscardedCleanup()
	{
		auto ID = fg_RandomID(mp_Owned);
		auto &Entry = mp_Owned[ID];
		g_OnScopeExit / [this, ID]
			{
				mp_Owned.f_Remove(ID);
			}
		;
		co_await fg_Suspend();
		co_return Entry.m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: {{.*}}
	}

	TCFuture<int> f_EarlyCleanup()
	{
		auto ID = fg_RandomID(mp_Owned);
		auto Cleanup = co_await fg_AsyncDestroy
			(
				[this, ID]() -> TCFuture<int>
				{
					mp_Owned.f_Remove(ID);
					co_return 0;
				}
			)
		;
		auto &Entry = mp_Owned[ID];
		co_await fg_Move(Cleanup);
		co_await fg_Suspend();
		co_return Entry.m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: {{.*}}
	}

	// A flag compared to be set is tested.
	TCFuture<int> f_ComparedFlag()
	{
		auto Guard = co_await fg_OnResume
			(
				[this]() -> CExceptionPointer
				{
					if (mp_State.m_bStoppingApp == true)
						return fg_MakeError("Stopped");
					return {};
				}
			)
		;
		auto *pEntry = mp_Stopped.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	DMibActorEntries(Owner) TCMap<CStr, CDownload> mp_Owned;
	DMibActorEntries(StopApp) TCMap<CStr, CDownload> mp_Stopped;
};

struct CRound31 : CActor
{
	// A lambda doesn't own the entry the call around it added.
	TCFuture<int> f_CapturedOwned()
	{
		auto ID = fg_RandomID(mp_Owned);
		auto *pEntry = &mp_Owned[ID];
		auto Cleanup = g_OnScopeExit / [this, ID]
			{
				mp_Owned.f_Remove(ID);
			}
		;
		return fg_CallSafe
			(
				[pEntry]() -> TCFuture<int>
				{
					co_await fg_Suspend();
					co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: {{.*}}
				}
			)
		;
	}

	// A guard in an inner scope runs the cleanup when that scope ends.
	TCFuture<int> f_InnerGuard()
	{
		auto ID = fg_RandomID(mp_Owned);
		auto &Entry = mp_Owned[ID];
		{
			auto Cleanup = co_await fg_AsyncDestroy
				(
					[this, ID]() -> TCFuture<int>
					{
						mp_Owned.f_Remove(ID);
						co_return 0;
					}
				)
			;
		}
		co_await fg_Suspend();
		co_return Entry.m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: {{.*}}
	}

	// Removing the entry of another fresh key leaves this one, and an
	// unchanged copy of `this` is the own object.
	TCFuture<int> f_OtherKey()
	{
		auto *pSelf = this;
		auto First = fg_RandomID(pSelf->mp_Owned);
		auto Second = fg_RandomID(mp_Owned);
		mp_Owned[First];
		auto &Entry = mp_Owned[Second];
		mp_Owned.f_Remove(First);
		co_await fg_Suspend();
		co_return Entry.m_Value;
	}

	// The data of another actor that a parameter points to.
	TCFuture<int> f_OtherActor(CRound31 *_pOther)
	{
		auto *pEntry = _pOther->mp_Plain.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: {{.*}}
	}

	// A flag read before a suspension in the condition doesn't tell about
	// after it.
	TCFuture<int> f_SuspendInCondition()
	{
		auto *pEntry = mp_Flagged.f_FindEqual("Key");
		auto pDeleted = pEntry->m_pDeleted;
		if (*pDeleted || (co_await fg_Suspend(), false))
			co_return 0;
		co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: {{.*}}
	}

	struct CFlagged
	{
		bool *m_pDeleted = nullptr;
		int m_Value = 0;
	};

	DMibActorEntries(Owner) TCMap<CStr, CDownload> mp_Owned;
	TCMap<CStr, CDownload> mp_Plain;
	TCMap<CStr, CFlagged> mp_Flagged;
};

struct CRound32Entry
{
	TCSharedPointer<bool> m_pDeleted;
	int m_Value = 0;
};

struct CRound32;

struct CRound32Pointer
{
	CRound32 *m_pActor;
	CRound32 *operator -> () const;
	void swap(CRound32Pointer &_Other);
};

struct CRound32 : CPhaseActor
{
	// A holder of the own object that is swapped is no longer it.
	TCFuture<int> f_Swapped(CRound32 *_pOther)
	{
		CRound32Pointer pThis{this};
		CRound32Pointer pOther{_pOther};
		pThis.swap(pOther);
		auto Guard = co_await pThis->f_CheckDestroyedOnResume();
		auto *pEntry = mp_Destroyed.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: {{.*}}
	}

	// A mutable callback that tests another flag than the one copied.
	TCFuture<int> f_ReplacedFlag(TCSharedPointer<bool> _pOther)
	{
		auto *pEntry = mp_Flagged.f_FindEqual("Key");
		auto pDeleted = pEntry->m_pDeleted;
		auto Guard = co_await fg_OnResume
			(
				[pDeleted, _pOther]() mutable -> CExceptionPointer
				{
					pDeleted = _pOther;
					if (*pDeleted)
						return fg_MakeError("Deleted");
					return {};
				}
			)
		;
		co_await fg_Suspend();
		co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: {{.*}}
	}

	// Pointers compared without a fresh lookup don't tell the entry is there.
	TCFuture<int> f_ComparedKept()
	{
		auto *pEntry = mp_Flagged.f_FindEqual("Key");
		auto *pOriginal = pEntry;
		auto &Entry = *pEntry;
		auto Guard = co_await fg_OnResume
			(
				[&]() -> CExceptionPointer
				{
					if (pEntry != pOriginal)
						return fg_MakeError("Changed");
					return {};
				}
			)
		;
		co_await fg_Suspend();
		co_return Entry.m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: {{.*}}
	}

	// A copied removal flag compared to be set.
	TCFuture<int> f_FlagComparedTrue()
	{
		auto *pEntry = mp_Flagged.f_FindEqual("Key");
		auto pDeleted = pEntry->m_pDeleted;
		co_await fg_Suspend();
		if (*pDeleted == true)
			co_return 0;
		co_return pEntry->m_Value;
	}

	DMibActorEntries(Destroy) TCMap<CStr, CDownload> mp_Destroyed;
	TCMap<CStr, CRound32Entry> mp_Flagged;
};

template <typename t_CType>
struct TCRound32Handle
{
	t_CType &m_Entry;
	t_CType &operator * ();
};

struct CRound32Template : CActor
{
	template <typename t_CType>
	TCRound32Handle<t_CType> f_Lookup();

	TCMap<CStr, CDownload> mp_Items;
};

// A dependent handle is known in the instantiations, also with an actor
// parameter.
template <typename t_CType>
TCFuture<int> fg_TemplateHandle(CRound32Template *_pActor)
{
	auto Entry = _pActor->f_Lookup<t_CType>();
	co_await fg_Suspend();
	co_return (*Entry).m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: {{.*}}
}

template TCFuture<int> fg_TemplateHandle<CDownload>(CRound32Template *_pActor);

namespace NMib::NException
{
	CExceptionPointer fg_CurrentException();
}

struct CRound33Handle
{
	CDownload &m_Entry;
	CRound33Handle(CDownload &_Entry, int _Tag);
};

struct CRound33 : CPhaseActor
{
	DMibActorPhaseGuard(Destroy) static TCFuture<COnResumeScope> fs_Guard(CRound33 *_pThis);

	// A static guard given another actor doesn't guard this one.
	TCFuture<int> f_StaticGuard(CRound33 *_pOther)
	{
		auto Guard = co_await fs_Guard(_pOther);
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: {{.*}}
	}

	TCFuture<int> f_OwnStaticGuard()
	{
		auto Guard = co_await fs_Guard(this);
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	// Both branches return an error, and the current exception may be none.
	TCFuture<int> f_BranchErrors(bool _bFirst)
	{
		auto Guard = co_await fg_OnResume
			(
				[this, _bFirst]() -> CExceptionPointer
				{
					if (f_IsDestroyed())
					{
						if (_bFirst)
							return fg_MakeError("First");
						else
							return fg_MakeError("Second");
					}
					return {};
				}
			)
		;
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_CurrentException()
	{
		auto Guard = co_await fg_OnResume
			(
				[this]() -> CExceptionPointer
				{
					if (f_IsDestroyed())
						return NException::fg_CurrentException();
					return {};
				}
			)
		;
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: {{.*}}
	}

	// A handle made with more arguments.
	TCFuture<int> f_HandleArguments()
	{
		CRound33Handle Handle(mp_Plain["Key"], 0);
		co_await fg_Suspend();
		co_return Handle.m_Entry.m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: {{.*}}
	}

	DMibActorEntries(Destroy) TCMap<CStr, CDownload> mp_Items;
	TCMap<CStr, CDownload> mp_Plain;
};

// A lookup captured through an alias.
void fg_CapturedAlias(CRound31 *_pActor)
{
	auto *pEntry = _pActor->mp_Plain.f_FindEqual("Key");
	auto *pAlias = pEntry;
	auto fLambda = [pAlias]() -> TCFuture<int>
		{
			co_await fg_Suspend();
			co_return pAlias->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: {{.*}}
		}
	;
	(void)fg_CallSafe(fLambda);
}

struct CRound34Result
{
	CDownload &m_Entry;
	CDownload &operator * ();
};

struct CRound34Map
{
	CRound34Result operator () (CStr const &_Key);
};

struct CRound34 : CPhaseActor
{
	// Either value of a conditional, and a handle made with more arguments
	// keeps the protection of its lookup.
	TCFuture<int> f_ConditionalHandle(bool _bFirst)
	{
		auto Entry = _bFirst ? mp_Handles("First") : mp_Handles("Second");
		co_await fg_Suspend();
		co_return (*Entry).m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: {{.*}}
	}

	TCFuture<int> f_GuardedHandle()
	{
		auto Guard = co_await f_CheckDestroyedOnResume();
		auto &Entry = mp_Items["Key"];
		CRound33Handle Handle(Entry, 0);
		co_await fg_Suspend();
		co_return Handle.m_Entry.m_Value;
	}

	DMibActorEntries(Destroy) TCMap<CStr, CDownload> mp_Items;
	TCMap<CStr, CDownload> mp_Plain;
	CRound34Map mp_Handles;
};

struct CRound35 : CDistributedAppActor
{
	// An iterator kept across a suspension.
	TCFuture<int> f_Iterator()
	{
		auto iEntry = mp_Plain.f_GetIterator();
		co_await fg_Suspend();
		co_return iEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: {{.*}}
	}

	// Casting the guard to void keeps it.
	TCFuture<int> f_VoidGuard()
	{
		auto Guard = co_await fp_CheckStoppedOrDestroyedOnResume();
		(void)Guard;
		auto *pEntry = mp_Stopped.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	// A cleanup called early removes the entry right away.
	TCFuture<int> f_EarlyCleanup()
	{
		auto ID = fg_RandomID(mp_Owned);
		auto &Entry = mp_Owned[ID];
		auto Cleanup = g_OnScopeExit / [this, ID]
			{
				mp_Owned.f_Remove(ID);
			}
		;
		Cleanup();
		co_await fg_Suspend();
		co_return Entry.m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: {{.*}}
	}

	// Pointer arithmetic stays in the data.
	TCFuture<int> f_Offset(int _Index)
	{
		auto *pEntry = mp_Plain.f_FindEqual("Key") + _Index;
		co_await fg_Suspend();
		co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: {{.*}}
	}

	TCMap<CStr, CDownload> mp_Plain;
	DMibActorEntries(StopApp) TCMap<CStr, CDownload> mp_Stopped;
	DMibActorEntries(Owner) TCMap<CStr, CDownload> mp_Owned;
};

struct CRound35Cleared : CActor
{
	// A cleanup cleared on success doesn't remove the entry before then.
	TCFuture<int> f_ClearedCleanup()
	{
		auto ID = fg_RandomID(mp_Owned);
		auto &Entry = mp_Owned[ID];
		auto Cleanup = co_await fg_AsyncDestroy
			(
				[this, ID]() -> TCFuture<int>
				{
					mp_Owned.f_Remove(ID);
					co_return 0;
				}
			)
		;
		co_await fg_Suspend();
		Cleanup.f_Clear();
		co_return Entry.m_Value;
	}

	DMibActorEntries(Owner) TCMap<CStr, CDownload> mp_Owned;
};

struct CRound36 : CDistributedAppActor
{
	// A jump back can use the entry again after it is removed.
	TCFuture<int> f_JumpBack()
	{
		auto ID = fg_RandomID(mp_Owned);
		auto &Entry = mp_Owned[ID];
	Again:
		co_await fg_Suspend();
		int Copy = Entry.m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: {{.*}}
		mp_Owned.f_Remove(ID);
		if (Copy)
			goto Again;
		co_return 0;
	}

	// An iterator that moves on is at entries the coroutine doesn't own.
	TCFuture<int> f_Advanced()
	{
		auto ID = fg_RandomID(mp_Owned);
		mp_Owned[ID];
		auto Iterator = mp_Owned.f_GetIterator(ID);
		++Iterator;
		if (!Iterator)
			co_return 0;
		co_await fg_Suspend();
		co_return Iterator->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: {{.*}}
	}

	DMibActorEntries(Owner) TCMap<CStr, CDownload> mp_Owned;
};

// A static getter of a phase flag tells about the actor it is given.
struct CRound37StaticFlag : CDistributedAppActor
{
	DMibActorEntries(StopApp) TCMap<CStr, CDownload> mp_Items;

	DMibActorPhaseFlag(StopApp) static bool fs_IsStopping(CRound37StaticFlag *_pActor)
	{
		return _pActor->mp_State.m_bStoppingApp;
	}

	TCFuture<int> f_Other(CRound37StaticFlag *_pOther)
	{
		auto OnResume = co_await fg_OnResume
			(
				[this, _pOther]() -> CExceptionPointer
				{
					if (fs_IsStopping(_pOther))
						return fg_MakeError("Stopping");
					return {};
				}
			)
		;
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'pEntry' refers into the data of the actor and is used after a suspension; {{.*}}
	}

	TCFuture<int> f_Own()
	{
		auto OnResume = co_await fg_OnResume
			(
				[this]() -> CExceptionPointer
				{
					if (fs_IsStopping(this))
						return fg_MakeError("Stopping");
					return {};
				}
			)
		;
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}
};

// The re-validation of real code: an error converted to the exception pointer
// for a pointer that doesn't match the one found first.
struct CRound37Revalidated : CDistributedAppActor
{
	TCMap<CStr, CDownload> mp_Distributions;

	TCFuture<int> f_Run(CStr _Name)
	{
		CDownload *pDistribution;
		CDownload *pInitialDistribution = nullptr;
		auto OnResume = co_await fg_OnResume
			(
				[&]() -> CExceptionPointer
				{
					pDistribution = mp_Distributions.f_FindEqual(_Name);
					if (!pDistribution || (pInitialDistribution && pDistribution != pInitialDistribution))
						return CException("CException", "Distribution no longer exists");
					pInitialDistribution = pDistribution;
					return nullptr;
				}
			)
		;
		auto &Distribution = *pDistribution;
		co_await fg_Suspend();
		co_return Distribution.m_Value;
	}
};

// A comparison that only runs when another condition holds doesn't
// re-validate, a lookup assigned only in a branch isn't fresh, and an error
// in both branches of an `if` leaves with an error.
struct CEdgeReference : CActor
{
	TCMap<int, int> mp_Items;

	TCFuture<int> f_ShortCircuit(bool _bCheck)
	{
		int *pEntry = mp_Items.f_FindEqual(1);
		int &Entry = *pEntry;
		auto Guard = co_await fg_OnResume
			(
				[&]() -> CExceptionPointer
				{
					if (_bCheck && pEntry != mp_Items.f_FindEqual(1))
						return fg_MakeError("removed");
					return {};
				}
			)
		;
		co_await fg_Suspend();
		co_return Entry;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'Entry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
	}

	TCFuture<int> f_ConditionalLookup(bool _bRefresh)
	{
		int *pEntry = mp_Items.f_FindEqual(1);
		int &Entry = *pEntry;
		int *pFresh = pEntry;
		auto Guard = co_await fg_OnResume
			(
				[&]() -> CExceptionPointer
				{
					if (_bRefresh)
						pFresh = mp_Items.f_FindEqual(1);
					if (pEntry != pFresh)
						return fg_MakeError("removed");
					return {};
				}
			)
		;
		co_await fg_Suspend();
		co_return Entry;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'Entry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
	}

	TCFuture<int> f_TwoErrors(bool _bDetailed)
	{
		int *pEntry = mp_Items.f_FindEqual(1);
		int &Entry = *pEntry;
		auto Guard = co_await fg_OnResume
			(
				[&]() -> CExceptionPointer
				{
					if (pEntry != mp_Items.f_FindEqual(1))
					{
						if (_bDetailed)
							return fg_MakeError("removed with details");
						else
							return fg_MakeError("removed");
					}
					return {};
				}
			)
		;
		co_await fg_Suspend();
		co_return Entry;
	}
};

// A null test of the fresh lookup doesn't re-validate the pointer kept from
// before, a structured binding of a lookup and a lookup behind a comma are
// lookups, a scope exit uses what it captures where it runs, and a lambda
// that only tests a pointer doesn't use the entry.
struct CEdgeEntry
{
	int m_Value;
};
struct CEdgeLookups : CActor
{
	TCMap<int, CEdgeEntry> mp_Entries;
	TCMap<int, int> mp_Values;
	void f_Prepare();

	TCFuture<int> f_FreshNullTest(int _Key)
	{
		CEdgeEntry *pEntry = &mp_Entries[_Key];
		auto &Entry = *pEntry;
		auto OnResume = co_await fg_OnResume
			(
				[&]() -> CExceptionPointer
				{
					auto *pNew = mp_Entries.f_FindEqual(_Key);
					if (pNew && pEntry != pNew)
						return fg_MakeError("Changed");
					return {};
				}
			)
		;
		co_await fg_Suspend();
		co_return Entry.m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'Entry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
	}

	TCFuture<int> f_Binding(int _Key)
	{
		auto &[Value] = mp_Entries[_Key];
		co_await fg_Suspend();
		co_return Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'Value' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
	}

	TCFuture<int> f_Comma(int _Key)
	{
		CEdgeEntry *pEntry = (f_Prepare(), mp_Entries.f_FindEqual(_Key));
		co_await fg_Suspend();
		co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
	}

	TCFuture<int> f_ScopeExit()
	{
		int *pValue = mp_Values.f_FindEqual(0);
		auto Cleanup = g_OnScopeExit / [pValue]
// CHECK-MESSAGES: :[[@LINE-1]]:35: warning: 'pValue' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
			{
				*pValue = 0;
			}
		;
		co_await fg_Suspend();
		co_return 0;
	}

	TCFuture<int> f_OnlyTested()
	{
		int *pValue = mp_Values.f_FindEqual(0);
		auto fNonNull = [pValue]
			{
				return pValue != nullptr;
			}
		;
		co_await fg_Suspend();
		co_return fNonNull();
	}
};

// A scope exit that tests a removal flag copied from the entry before it uses
// the entry doesn't use a removed one; another flag doesn't tell.
struct CFlaggedEdgeEntry
{
	int m_Value;
	TCSharedPointer<bool> m_pDeleted;
	TCSharedPointer<bool> m_pBusy;
};
struct CFlaggedScopeExits : CActor
{
	TCMap<int, CFlaggedEdgeEntry> mp_Entries;

	TCFuture<int> f_ReturnIfDeleted()
	{
		CFlaggedEdgeEntry *pEntry = mp_Entries.f_FindEqual(0);
		auto Cleanup = g_OnScopeExit / [pDeleted = pEntry->m_pDeleted, pEntry]
			{
				if (*pDeleted)
					return;
				pEntry->m_Value = 0;
			}
		;
		co_await fg_Suspend();
		co_return 0;
	}

	TCFuture<int> f_UnlessDeleted()
	{
		CFlaggedEdgeEntry *pEntry = mp_Entries.f_FindEqual(0);
		TCSharedPointer<bool> pDeleted = pEntry->m_pDeleted;
		auto Cleanup = g_OnScopeExit / [pDeleted, pEntry]
			{
				if (!*pDeleted)
					pEntry->m_Value = 0;
			}
		;
		co_await fg_Suspend();
		co_return 0;
	}

	TCFuture<int> f_OtherFlag()
	{
		CFlaggedEdgeEntry *pEntry = mp_Entries.f_FindEqual(0);
		TCSharedPointer<bool> pOther = pEntry->m_pBusy;
		auto Cleanup = g_OnScopeExit / [pOther, pEntry]
// CHECK-MESSAGES: :[[@LINE-1]]:43: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
			{
				if (!*pOther)
					pEntry->m_Value = 0;
			}
		;
		co_await fg_Suspend();
		co_return 0;
	}
};

// What a pointer member of a guarded entry points to can be replaced while the
// entry stays, an iterator moved on through a reference or with f_Next is at
// the entry of another call, and removing the entry of another fresh key
// through a copy of it leaves this one.
struct CPointeeEntry
{
	TCSharedPointer<int> m_pValue;
};
struct CEdgeOwners : CPhaseActor
{
	DMibActorEntries(Destroy) TCMap<int, CPointeeEntry> mp_Pointees;
	DMibActorEntries(Owner) TCMap<CStr, int> mp_Owned;

	TCFuture<int> f_Pointee()
	{
		auto Guard = co_await f_CheckDestroyedOnResume();
		auto &Value = *mp_Pointees[1].m_pValue;
		co_await fg_Suspend();
		co_return Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'Value' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
	}

	TCFuture<int> f_IteratorReference()
	{
		auto ID = fg_RandomID(mp_Owned);
		mp_Owned[ID];
		auto &&iEntry = mp_Owned.f_GetIterator(ID);
		++iEntry;
		if (!iEntry)
			co_return 0;
		co_await fg_Suspend();
		co_return *iEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: 'iEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
	}

	TCFuture<int> f_IteratorNext()
	{
		auto ID = fg_RandomID(mp_Owned);
		mp_Owned[ID];
		auto iEntry = mp_Owned.f_GetIterator(ID);
		iEntry.f_Next();
		if (!iEntry)
			co_return 0;
		co_await fg_Suspend();
		co_return *iEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: 'iEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
	}

	TCFuture<int> f_RemoveCopy()
	{
		auto First = fg_RandomID(mp_Owned);
		mp_Owned[First];
		auto Second = fg_RandomID(mp_Owned);
		auto &Entry = mp_Owned[Second];
		auto Copy = First;
		mp_Owned.f_Remove(Copy);
		co_await fg_Suspend();
		co_return Entry;
	}
};

// Testing a pointer kept from before for null doesn't re-validate it.
struct CNullTested : CActor
{
	TCMap<int, int> mp_Items;

	TCFuture<int> f_Run()
	{
		int *pValue = mp_Items.f_FindEqual(1);
		auto Guard = co_await fg_OnResume
			(
				[&]() -> CExceptionPointer
				{
					if (!pValue)
						return fg_MakeError("missing");
					return {};
				}
			)
		;
		co_await fg_Suspend();
		co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: 'pValue' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
	}
};

// A copy of the state that a lambda captures doesn't see the phase flags of
// the actor change.
struct CCopiedState : CDistributedAppActor
{
	DMibActorEntries(StopApp) TCMap<CStr, int> mp_Items;

	TCFuture<int> f_Run()
	{
		auto &State = mp_State;
		auto Guard = co_await fg_OnResume
			(
				[State]() -> CExceptionPointer
				{
					if (State.m_bStoppingApp)
						return fg_MakeError("stopping");
					return {};
				}
			)
		;
		auto *pEntry = mp_Items.f_FindEqual("key");
		co_await fg_Suspend();
		co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: 'pEntry' refers into the data of the actor and is used after a suspension; 'mp_Items' loses entries only in StopApp, so a guard of that phase that is alive at the use, like fp_CheckStoppedOrDestroyedOnResume, makes it safe [malterlib-coroutine-actor-reference]
	}
};

// A fresh key moved into the collection is gone from its variable, so what is
// looked up or removed by it after is another entry.
struct CMovedKeys : CPhaseActor
{
	DMibActorEntries(Owner) TCMap<CStr, int> mp_Owned;

	TCFuture<int> f_Read()
	{
		auto Key = fg_RandomID(mp_Owned);
		mp_Owned[fg_Move(Key)];
		auto &Entry = mp_Owned[Key];
		co_await fg_Suspend();
		co_return Entry;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'Entry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
	}
};

// Starting a phase from the coroutine itself removes the entries of the phase
// where it does, whatever guard is alive; a pointer to the state of the actor
// is its own state.
struct CStartsOwnPhase : CDistributedAppActor
{
	DMibActorEntries(StopApp) TCMap<CStr, int> mp_Items;

	DMibActorPhaseEntry(StopApp) TCFuture<int> f_StopNow()
	{
		mp_State.m_bStoppingApp = true;
		mp_Items.f_Clear();
		co_return 0;
	}

	TCFuture<int> f_Run()
	{
		auto Guard = co_await fp_CheckStoppedOrDestroyedOnResume();
		auto &Entry = mp_Items["k"];
		co_await fg_Suspend();
		co_await f_StopNow();
		co_return Entry;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'Entry' refers into the data of the actor and is used after a suspension; 'mp_Items' loses entries only in StopApp, so a guard of that phase that is alive at the use, like fp_CheckStoppedOrDestroyedOnResume, makes it safe [malterlib-coroutine-actor-reference]
	}

	TCFuture<int> f_PointerToState()
	{
		auto *pState = &mp_State;
		auto Guard = co_await fg_OnResume
			(
				[pState]() -> CExceptionPointer
				{
					if (pState->m_bStoppingApp)
						return fg_MakeError("stopping");
					return {};
				}
			)
		;
		auto *pEntry = mp_Items.f_FindEqual("k");
		co_await fg_Suspend();
		co_return *pEntry;
	}
};

// A refresh on only some paths of the callback doesn't re-validate.
struct CConditionalRefresh : CActor
{
	TCMap<int, CEdgeEntry> mp_Entries;

	TCFuture<int> f_Run(bool _bRefresh)
	{
		auto *pEntry = mp_Entries.f_FindEqual(0);
		auto OnResume = co_await fg_OnResume
			(
				[&]() -> CExceptionPointer
				{
					if (_bRefresh)
						pEntry = mp_Entries.f_FindEqual(0);
					return {};
				}
			)
		;
		co_await fg_Suspend();
		co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
	}
};

// What a branch that leaves with an error assigns doesn't matter where the
// coroutine goes on.
struct CClearedOnError : CActor
{
	TCMap<int, CFlaggedEdgeEntry> mp_Entries;

	TCFuture<int> f_Run()
	{
		CFlaggedEdgeEntry *pEntry = &mp_Entries[1];
		auto OnResume = co_await fg_OnResume
			(
				[&, pDeleted = pEntry->m_pDeleted]() -> CExceptionPointer
				{
					if (*pDeleted)
					{
						pEntry = nullptr;
						return fg_MakeError("removed");
					}
					return {};
				}
			)
		;
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}
};

// A removal flag replaced by another one doesn't tell about the entry.
struct CReplacedFlag : CActor
{
	struct CEntry
	{
		int m_Value = 0;
		TCSharedPointer<bool> m_pDeleted;
	};
	TCMap<CStr, CEntry> mp_Entries;

	TCFuture<int> f_Run(TCSharedPointer<bool> _pUnrelated)
	{
		auto *pEntry = mp_Entries.f_FindEqual("Key");
		auto pDeleted = pEntry->m_pDeleted;
		pDeleted = _pUnrelated;
		auto Cleanup = g_OnScopeExit / [pEntry, pDeleted]
// CHECK-MESSAGES: :[[@LINE-1]]:35: warning: 'pEntry' refers into the data of the actor and is used after a suspension, {{.*}}
			{
				if (*pDeleted)
					return;
				pEntry->m_Value = 1;
			}
		;
		co_await fg_Suspend();
		co_return 0;
	}
};

// A lambda held in a local and run by fg_CallSafe after a suspension, and a
// phase entry called through a local pointer to it.
struct CCallSafeHeld : CPhaseActor
{
	TCMap<CStr, int> mp_Items;

	TCFuture<int> f_Run()
	{
		auto *pValue = mp_Items.f_FindEqual("Key");
		auto fRead = [pValue]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:17: warning: 'pValue' refers into the data of the actor and is used after a suspension, {{.*}}
			{
				co_return *pValue;
			}
		;
		co_await fg_Suspend();
		co_return co_await fg_CallSafe(fRead);
	}
};
struct CStoredEntry : CDistributedAppActor
{
	DMibActorEntries(StopApp) TCMap<CStr, int> mp_Items;

	TCFuture<int> f_Run()
	{
		auto OnResume = co_await fp_CheckStoppedOrDestroyedOnResume();
		auto *pValue = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		auto fStop = &CDistributedAppActor::f_StopApp;
		co_await (this->*fStop)();
		co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: 'pValue' refers into the data of the actor and is used after a suspension; {{.*}}
	}
};

// A phase entry called in a lambda that is called.
struct CStopInLambda : CDistributedAppActor
{
	DMibActorEntries(StopApp) TCMap<CStr, int> mp_Items;

	DMibActorPhaseEntry(StopApp) TCFuture<int> f_StopNow();

	TCFuture<int> f_Run()
	{
		auto OnResume = co_await fp_CheckStoppedOrDestroyedOnResume();
		auto *pValue = mp_Items.f_FindEqual("Key");
		auto fStop = [this]
			{
				return f_StopNow();
			}
		;
		co_await fStop();
		co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: 'pValue' refers into the data of the actor and is used after a suspension; {{.*}}
	}
};

// A resume callback that starts the phase it tests for isn't a guard of it.
struct CStartingGuard : CDistributedAppActor
{
	DMibActorEntries(StopApp) TCMap<CStr, int> mp_Items;

	DMibActorPhaseEntry(StopApp) TCFuture<int> f_StopNow();

	TCFuture<int> f_Read()
	{
		int nResumes = 0;
		auto OnResume = co_await fg_OnResume
			(
				[this, &nResumes]() -> CExceptionPointer
				{
					if (mp_State.m_bStoppingApp)
						return fg_MakeError("Stopped");
					if (++nResumes == 2)
						(void)f_StopNow();
					return {};
				}
			)
		;
		auto *pValue = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: 'pValue' refers into the data of the actor and is used after a suspension; {{.*}}
	}
};

// Reading what a pointer points to doesn't tell it is still there, and a
// refresh on some paths only doesn't look it up again.
struct CReadNotValidated : CActor
{
	TCMap<CStr, int> mp_Items;

	TCFuture<int> f_Read()
	{
		int *pEntry = mp_Items.f_FindEqual("Key");
		auto OnResume = co_await fg_OnResume
			(
				[&]() -> CExceptionPointer
				{
					if (*pEntry == 0)
// CHECK-MESSAGES: :[[@LINE-1]]:11: warning: 'pEntry' refers into the data of the actor and is used after a suspension, {{.*}}
						return fg_MakeError("Invalid");
					return {};
				}
			)
		;
		co_await fg_Suspend();
		co_return *pEntry;
	}

	static void fs_Refresh(int *&_pEntry, TCMap<CStr, int> &_Items);

	TCFuture<int> f_MaybeRefreshed(bool _bRefresh)
	{
		int *pEntry = mp_Items.f_FindEqual("Key");
		auto OnResume = co_await fg_OnResume
			(
				[&]() -> CExceptionPointer
				{
					if (_bRefresh)
						fs_Refresh(pEntry, mp_Items);
					return {};
				}
			)
		;
		co_await fg_Suspend();
		co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: 'pEntry' refers into the data of the actor and is used after a suspension, {{.*}}
	}
};

// A lambda dispatched after a suspension uses what it captured then.
struct CDispatchedStale : CActor
{
	TCMap<CStr, int> mp_Items;

	TCFuture<int> f_Read()
	{
		int *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return co_await (g_Dispatch / [pEntry] { return *pEntry; });
// CHECK-MESSAGES: :[[@LINE-1]]:37: warning: 'pEntry' refers into the data of the actor and is used after a suspension, {{.*}}
	}
};

// A callback that starts the phase in a lambda it calls isn't a guard of it,
// assigning a pointer kept from before doesn't look the entry up again, a
// comparison whose result is discarded tells nothing, and a lambda called
// through its call operator by name uses what it captures.
struct CRound52 : CDistributedAppActor
{
	DMibActorEntries(StopApp) TCMap<CStr, int> mp_Items;
	TCMap<CStr, int> mp_Other;

	DMibActorPhaseEntry(StopApp) TCFuture<int> f_StopNow();

	TCFuture<int> f_StartedInLambda()
	{
		int nResumes = 0;
		auto OnResume = co_await fg_OnResume
			(
				[this, &nResumes]() -> CExceptionPointer
				{
					if (mp_State.m_bStoppingApp)
						return fg_MakeError("Stopped");
					if (++nResumes == 2)
					{
						auto fStop = [this] { return f_StopNow(); };
						(void)fStop();
					}
					return {};
				}
			)
		;
		auto *pValue = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: 'pValue' refers into the data of the actor and is used after a suspension; 'mp_Items' loses entries only in StopApp, so a guard of that phase that is alive at the use, like fp_CheckStoppedOrDestroyedOnResume, makes it safe [malterlib-coroutine-actor-reference]
	}

	TCFuture<int> f_AssignedSaved()
	{
		int *pValue = mp_Other.f_FindEqual("Key");
		int *pSaved = pValue;
		auto OnResume = co_await fg_OnResume
			(
				[&]() -> CExceptionPointer
				{
					pValue = pSaved;
					return {};
				}
			)
		;
		co_await fg_Suspend();
		co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: 'pValue' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
	}

	TCFuture<int> f_Discarded()
	{
		int *pValue = mp_Other.f_FindEqual("Key");
		auto OnResume = co_await fg_OnResume
			(
				[&]() -> CExceptionPointer
				{
					(void)(pValue != mp_Other.f_FindEqual("Key"));
					return {};
				}
			)
		;
		co_await fg_Suspend();
		co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: 'pValue' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
	}

	TCFuture<int> f_CalledByName()
	{
		auto *pValue = mp_Other.f_FindEqual("Key");
		auto fRead = [pValue] { return *pValue; };
// CHECK-MESSAGES: :[[@LINE-1]]:17: warning: 'pValue' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
		co_await fg_Suspend();
		co_return fRead.operator()();
	}
};

// A phase started in a lambda that a lambda called here calls, and a
// collection held in an element of a member array of the own actor.
struct CRound53Arrays
{
	DMibActorEntries(StopApp) TCMap<CStr, int> m_Items;
};
struct CRound53 : CDistributedAppActor
{
	DMibActorEntries(StopApp) TCMap<CStr, int> mp_Items;
	CRound53Arrays mp_States[2];

	DMibActorPhaseEntry(StopApp) TCFuture<int> f_StopNow();

	TCFuture<int> f_Nested()
	{
		auto OnResume = co_await fp_CheckStoppedOrDestroyedOnResume();
		auto &Value = mp_Items["Key"];
		co_await fg_Suspend();
		auto fOuter = [this]
			{
				auto fInner = [this] { return f_StopNow(); };
				return fInner();
			}
		;
		co_await fOuter();
		co_return Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'Value' refers into the data of the actor and is used after a suspension; 'mp_Items' loses entries only in StopApp, so a guard of that phase that is alive at the use, like fp_CheckStoppedOrDestroyedOnResume, makes it safe [malterlib-coroutine-actor-reference]
	}

	TCFuture<int> f_Array()
	{
		auto OnResume = co_await fp_CheckStoppedOrDestroyedOnResume();
		auto &Value = mp_States[0].m_Items["Key"];
		co_await fg_Suspend();
		co_return Value;
	}
};

// A guard of another actor through an owning pointer doesn't guard this one,
// and an atomic flag read through `load()` is tested.
namespace std
{
	template <typename t_CType>
	struct atomic
	{
		t_CType load() const;
	};
}
struct CRound54Other : CDistributedAppActor
{
	NStorage::TCSharedPointer<CDistributedAppActor> mp_pOther;
	DMibActorEntries(StopApp) TCMap<CStr, int> mp_Items;

	TCFuture<int> f_Run()
	{
		auto OnResume = co_await mp_pOther->fp_CheckStoppedOrDestroyedOnResume();
		auto &Value = mp_Items["Key"];
		co_await fg_Suspend();
		co_return Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'Value' refers into the data of the actor and is used after a suspension; {{.*}}
	}
};
struct CRound54Atomic : CActor
{
	DMibActorPhaseFlag(StopApp) std::atomic<bool> mp_bStopping;
	DMibActorEntries(StopApp) TCMap<CStr, int> mp_Items;

	TCFuture<int> f_Run()
	{
		auto OnResume = co_await fg_OnResume
			(
				[this]() -> CExceptionPointer
				{
					if (mp_bStopping.load())
						return fg_MakeError("Stopped");
					return {};
				}
			)
		;
		auto &Value = mp_Items["Key"];
		co_await fg_Suspend();
		co_return Value;
	}
};

// What a pointer member of a guarded entry points to, also through a local
// reference to the member, can be replaced while the entry stays.
struct CRound55Pointee : CPhaseActor
{
	DMibActorEntries(Destroy) TCMap<int, CPointeeEntry> mp_Pointees;

	TCFuture<int> f_Run()
	{
		auto Guard = co_await f_CheckDestroyedOnResume();
		auto &pPointer = mp_Pointees[1].m_pValue;
		auto &Value = *pPointer;
		co_await fg_Suspend();
		co_return Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'Value' refers into the data of the actor and is used after a suspension, {{.*}}
	}

	void f_Replace()
	{
		mp_Pointees[1].m_pValue = {};
	}
};

// Another actor through `f_Get()` isn't guarded by a guard of this one, and a
// guard awaited through a local awaiter guards.
template <typename t_CType>
struct TCRound56Pointer
{
	t_CType *f_Get() const;
	t_CType *operator->() const;
};
struct CRound56Other : CPhaseActor
{
	DMibActorEntries(Destroy) TCMap<CStr, int> mp_Items;
};
struct CRound56Guarded : CPhaseActor
{
	TCRound56Pointer<CRound56Other> mp_pOther;
	DMibActorEntries(Destroy) TCMap<CStr, int> mp_Items;

	TCFuture<int> f_Other()
	{
		auto Guard = co_await f_CheckDestroyedOnResume();
		auto *pEntry = mp_pOther.f_Get()->mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: 'pEntry' refers into the data of the actor and is used after a suspension{{.*}}
	}

	TCFuture<int> f_Stored()
	{
		auto Awaiter = f_CheckDestroyedOnResume();
		auto Guard = co_await fg_Move(Awaiter);
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return *pEntry;
	}
};

// The value of a pointer member of an entry points to an object of its own,
// and a comparison that is only given to a call isn't acted on.
struct CRound57PointerEntry
{
	int *m_pValue;
};
void fg_Round57Log(bool _bSame);
struct CRound57Copied : CActor
{
	DMibActorEntries() TCMap<int, CRound57PointerEntry> mp_Items;
	TCMap<int, int> mp_Values;

	TCFuture<int> f_Copy()
	{
		int *pValue = mp_Items[1].m_pValue;
		co_await fg_Suspend();
		co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: 'pValue' refers into the data of the actor and is used after a suspension{{.*}}
	}

	TCFuture<int> f_Logged()
	{
		int *pEntry = mp_Values.f_FindEqual(1);
		auto OnResume = co_await fg_OnResume
			(
				[&]() -> CExceptionPointer
				{
					fg_Round57Log(pEntry != mp_Values.f_FindEqual(1));
					return nullptr;
				}
			)
		;
		co_await fg_Suspend();
		co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: 'pEntry' refers into the data of the actor and is used after a suspension{{.*}}
	}
};

// A helper that starts the phase, a copy of a pointer to the function that
// does, what f_Get() of an owning pointer in an entry gives, what an optional
// holds, and an element of an array held in the actor.
struct CRound58Stop : CDistributedAppActor
{
	DMibActorEntries(StopApp) TCMap<int, int> mp_Items;

	TCFuture<int> fp_StopApp() override
	{
		mp_Items.f_Clear();
		co_return 0;
	}

	TCFuture<int> f_StopWrapper()
	{
		co_return co_await f_StopApp();
	}

	TCFuture<int> f_Helper()
	{
		auto Guard = co_await fp_CheckStoppedOrDestroyedOnResume();
		auto &Entry = mp_Items[1];
		co_await f_StopWrapper();
		co_return Entry;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'Entry' refers into the data of the actor and is used after a suspension{{.*}}
	}

	TCFuture<int> f_PointerCopy()
	{
		auto Guard = co_await fp_CheckStoppedOrDestroyedOnResume();
		auto &Entry = mp_Items[1];
		auto fStop = &CRound58Stop::f_StopApp;
		auto fStopCopy = fStop;
		co_await (this->*fStopCopy)();
		co_return Entry;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'Entry' refers into the data of the actor and is used after a suspension{{.*}}
	}
};
template <typename t_CType>
struct TCRound58Unique
{
	t_CType *f_Get() const;
	~TCRound58Unique();
};
struct CRound58UniqueEntry
{
	TCRound58Unique<int> m_pValue;
};
template <typename t_CType>
struct TCOptional
{
	t_CType &operator*();
	void f_Clear();
};
struct CRound58Lookups : CActor
{
	TCMap<int, CRound58UniqueEntry> mp_Items;
	TCOptional<int> mp_Value;
	int mp_Values[8];

	TCFuture<int> f_Get()
	{
		int *pValue = mp_Items[1].m_pValue.f_Get();
		co_await fg_Suspend();
		co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: 'pValue' refers into the data of the actor and is used after a suspension{{.*}}
	}

	TCFuture<int> f_Optional()
	{
		auto &Value = *mp_Value;
		co_await fg_Suspend();
		co_return Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'Value' refers into the data of the actor and is used after a suspension{{.*}}
	}

	TCFuture<int> f_Array(int _Index)
	{
		int &Value = mp_Values[_Index];
		co_await fg_Suspend();
		co_return Value;
	}
};

// A handle made with parentheses, the same owned collection through
// `operator->`, a stored fg_OnResume awaiter, and the old pointer that
// fg_Exchange gives.
struct CRound59Handle
{
	int &m_Value;
};
struct CRound59State
{
	DMibActorEntries(Owner) TCMap<CStr, int> mp_Items;
};
struct CRound59Actor : CActor
{
	TCMap<int, int> mp_Items;
	TCSharedPointer<CRound59State> mp_pState;

	TCFuture<int> f_Paren()
	{
		CRound59Handle Handle(mp_Items[1]);
		co_await fg_Suspend();
		co_return Handle.m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'Handle' refers into the data of the actor and is used after a suspension{{.*}}
	}

	TCFuture<int> f_Owned()
	{
		auto Key = fg_RandomID(mp_pState->mp_Items);
		auto &Value = mp_pState->mp_Items[Key];
		co_await fg_Suspend();
		int Result = Value;
		mp_pState->mp_Items.f_Remove(Key);
		co_return Result;
	}

	TCFuture<int> f_Stored()
	{
		int *pValue = mp_Items.f_FindEqual(1);
		auto Awaiter = fg_OnResume
			(
				[&]() -> CExceptionPointer
				{
					if (pValue != mp_Items.f_FindEqual(1))
						return fg_MakeError("Removed");
					return {};
				}
			)
		;
		auto OnResume = co_await fg_Move(Awaiter);
		co_await fg_Suspend();
		co_return *pValue;
	}

	TCFuture<int> f_Exchanged()
	{
		int *pValue = mp_Items.f_FindEqual(1);
		int *pSaved = fg_Exchange(pValue, (int *)nullptr);
		co_await fg_Suspend();
		co_return *pSaved;
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: 'pSaved' refers into the data of the actor and is used after a suspension{{.*}}
	}
};

// An iterator assigned a lookup with its `operator=` is tracked, and assigning
// it again after a suspension looks the data up again.
struct CRound60Iterators : CActor
{
	TCMap<int, int> mp_Items;

	TCFuture<int> f_Assigned()
	{
		TCMap<int, int>::CIterator iEntry;
		iEntry = mp_Items.f_GetIterator(1);
		co_await fg_Suspend();
		co_return *iEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: 'iEntry' refers into the data of the actor and is used after a suspension, during which {{.*}}
	}

	TCFuture<int> f_AssignedAgain()
	{
		auto iEntry = mp_Items.f_GetIterator(1);
		co_await fg_Suspend();
		iEntry = mp_Items.f_GetIterator(1);
		co_return *iEntry;
	}
};

// A phase guard registered with its own awaiter, which continues right away
// when it registers, doesn't suspend.
struct CRound60GuardAwaiter : CSuspendNever
{
	bool await_ready() const noexcept
	{
		return false;
	}
	bool await_suspend(std::coroutine_handle<> _Handle);
	COnResumeScope await_resume() noexcept;
};

struct CRound60Guard : CActor
{
	TCMap<int, int> mp_Items;
	DMibActorPhaseGuard(Destroy) CRound60GuardAwaiter f_CheckDestroyedOnResume() const;

	TCFuture<int> f_Run()
	{
		auto &Entry = mp_Items[1];
		auto Guard = co_await f_CheckDestroyedOnResume();
		co_return Entry;
	}
};

// A removal before the lookup is of an entry that the lookup makes again.
struct CRound60RemovedBefore : CActor
{
	DMibActorEntries(Owner) TCMap<CStr, int> mp_Items;

	TCFuture<int> f_Run()
	{
		auto Key = fg_RandomID(mp_Items);
		mp_Items[Key] = 1;
		mp_Items.f_Remove(Key);

		auto &Entry = mp_Items[Key];
		co_await fg_Suspend();
		co_return Entry;
	}
};

// A callback that can return success before it looks the pointer up again
// doesn't re-validate it, and what it uses before it does is used stale on a
// later resume.
struct CRound60Entry
{
	int m_Value;
};
struct CRound60Map
{
	CRound60Entry *f_FindEqual(int _Key);
	void f_Remove(int _Key);
};
struct CRound60Callbacks : CActor
{
	CRound60Map m_Items;

	TCFuture<int> f_Skipped(bool _bSkip)
	{
		auto *pEntry = m_Items.f_FindEqual(1);
		auto OnResume = co_await fg_OnResume
			(
				[&]() -> CExceptionPointer
				{
					if (_bSkip)
						return nullptr;
					pEntry = m_Items.f_FindEqual(1);
					return nullptr;
				}
			)
		;
		co_await fg_Suspend();
		co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which {{.*}}
	}

	TCFuture<int> f_UsedBefore()
	{
		auto *pEntry = m_Items.f_FindEqual(1);
		auto OnResume = co_await fg_OnResume
			(
				[&]() -> CExceptionPointer
				{
					if (pEntry->m_Value == 0)
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which {{.*}}
						return fg_MakeError("Empty");
					pEntry = m_Items.f_FindEqual(1);
					return nullptr;
				}
			)
		;
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}
};

// The flag of an entry doesn't tell about what a pointer of it points to.
struct CRound60FlaggedEntry
{
	int *m_pValue;
	NMib::NStorage::TCSharedPointer<bool> m_pDeleted;
};
struct CRound60FlaggedMap
{
	CRound60FlaggedEntry &operator[](int _Key);
};
struct CRound60Flagged : CActor
{
	CRound60FlaggedMap m_Items;

	TCFuture<int> f_Read()
	{
		auto &Entry = m_Items[1];
		int *pValue = Entry.m_pValue;
		auto pDeleted = Entry.m_pDeleted;
		co_await fg_Suspend();
		if (*pDeleted)
			co_return 0;
		co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: 'pValue' refers into the data of the actor and is used after a suspension, during which {{.*}}
	}
};

// The cleanup of `fg_AsyncDestroy` awaited after a suspension uses what it
// captures then, and a scope exit cancelled, or run early, before a
// suspension doesn't run after it.
struct CRound60Cleanups : CActor
{
	TCMap<int, int> mp_Items;

	TCFuture<int> f_AsyncDestroy()
	{
		auto *pEntry = mp_Items.f_FindEqual(1);
		auto Cleanup = co_await fg_AsyncDestroy
			(
				[pEntry]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:6: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which {{.*}}
				{
					++*pEntry;
					co_return 0;
				}
			)
		;
		co_await fg_Suspend();
		co_await fg_Move(Cleanup);
		co_return 0;
	}

	TCFuture<int> f_Cancelled()
	{
		auto *pEntry = mp_Items.f_FindEqual(1);
		auto Cleanup = g_OnScopeExit / [pEntry]
			{
				++*pEntry;
			}
		;
		Cleanup.f_Clear();
		co_await fg_Suspend();
		co_return 0;
	}

	TCFuture<int> f_RunEarly()
	{
		auto *pEntry = mp_Items.f_FindEqual(1);
		auto Cleanup = g_OnScopeExit / [pEntry]
			{
				++*pEntry;
			}
		;
		Cleanup();
		co_await fg_Suspend();
		co_return 0;
	}
};

// A callback that starts a phase through a helper doesn't guard it.
struct CRound60StartedByHelper : CActor
{
	DMibActorPhaseFlag(StopApp) bool m_bStopping = false;
	DMibActorEntries(StopApp) TCMap<int, int> mp_Items;
	bool m_bStopNow = false;

	DMibActorPhase(StopApp) void fp_Clear()
	{
		mp_Items.f_Clear();
	}

	DMibActorPhaseEntry(StopApp) void f_StopApp()
	{
		m_bStopping = true;
		fp_Clear();
	}

	void f_StopWrapper()
	{
		f_StopApp();
	}

	TCFuture<int> f_Run()
	{
		auto OnResume = co_await fg_OnResume
			(
				[this]() -> CExceptionPointer
				{
					if (m_bStopping)
						return fg_MakeError("Stopping");
					if (m_bStopNow)
						f_StopWrapper();
					return {};
				}
			)
		;
		auto *pEntry = mp_Items.f_FindEqual(1);
		co_await fg_Suspend();
		co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: 'pEntry' refers into the data of the actor and is used after a suspension; 'mp_Items' loses entries only in StopApp, {{.*}}
	}
};

// A callback registered after a guard can start its phase after the guard
// ran, which it then doesn't see.
struct CRound61LaterCallback : CActor
{
	DMibActorPhaseFlag(StopApp) bool m_bStopping = false;
	DMibActorEntries(StopApp) TCMap<int, int> mp_Items;
	bool m_bStopNow = false;

	DMibActorPhaseEntry(StopApp) void f_StopApp()
	{
		m_bStopping = true;
		mp_Items.f_Clear();
	}

	TCFuture<int> f_Run()
	{
		auto Guard = co_await fg_OnResume
			(
				[this]() -> CExceptionPointer
				{
					if (m_bStopping)
						return fg_MakeError("Stopping");
					return {};
				}
			)
		;
		auto Stop = co_await fg_OnResume
			(
				[this]() -> CExceptionPointer
				{
					if (m_bStopNow)
						f_StopApp();
					return {};
				}
			)
		;
		auto &Entry = mp_Items[1];
		co_await fg_Suspend();
		co_return Entry;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'Entry' refers into the data of the actor and is used after a suspension; 'mp_Items' {{.*}}
	}
};

// The address of a lookup through `std::addressof`, and the pointer of an
// owning pointer member through `get()`, which lives as long as the actor.
namespace std
{
	template <typename t_CType>
	t_CType *addressof(t_CType &_Value) noexcept;

	template <typename t_CType>
	struct unique_ptr
	{
		~unique_ptr();
		t_CType *get() const noexcept;
		t_CType &operator*() const;
		t_CType *operator->() const;
	};
}
struct CRound61Addresses : CActor
{
	TCMap<int, int> mp_Items;
	std::unique_ptr<int> mp_pValue;

	TCFuture<int> f_AddressOf()
	{
		int *pEntry = std::addressof(mp_Items[1]);
		co_await fg_Suspend();
		co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which {{.*}}
	}

	TCFuture<int> f_Get()
	{
		int *pValue = mp_pValue.get();
		co_await fg_Suspend();
		co_return *pValue;
	}
};

// A comparison whose mismatch only logs doesn't re-validate, and a jump can
// pass the test of a removal flag.
void fg_Round61Log(char const *_pMessage);
struct CRound61FlaggedEntry
{
	int m_Value;
	NStorage::TCSharedPointer<bool> m_pDeleted;
};
struct CRound61Checks : CActor
{
	TCMap<int, int> mp_Items;
	TCMap<int, CRound61FlaggedEntry> mp_Entries;

	TCFuture<int> f_Logged()
	{
		int *pEntry = mp_Items.f_FindEqual(1);
		auto Guard = co_await fg_OnResume
			(
				[&]() -> CExceptionPointer
				{
					bool bSame = pEntry == mp_Items.f_FindEqual(1);
					if (!bSame)
						fg_Round61Log("Entry removed");
					return {};
				}
			)
		;
		co_await fg_Suspend();
		co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which {{.*}}
	}

	TCFuture<int> f_Jumped()
	{
		CRound61FlaggedEntry *pEntry = mp_Entries.f_FindEqual(1);
		auto pDeleted = pEntry->m_pDeleted;
		auto fRead = [pEntry, pDeleted]
// CHECK-MESSAGES: :[[@LINE-1]]:17: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which {{.*}}
			{
				goto Use;
				if (*pDeleted)
					return 0;
			Use:
				return pEntry->m_Value;
			}
		;
		co_await fg_Suspend();
		co_return fRead();
	}
};

// A member function of an entry called after an await in its arguments uses
// it after the suspension, and a reference to the state that holds the
// collection is the same state.
struct CRound62Entry
{
	int m_Value;
	void f_Update(int _Value)
	{
		m_Value = _Value;
	}
};
struct CRound62State
{
	DMibActorEntries(Owner) TCMap<CStr, int> mp_Items;
};
struct CRound62Actor : CActor
{
	TCMap<CStr, CRound62Entry> mp_Entries;
	CRound62State mp_State;

	TCFuture<int> f_InArgument(CStr _Key)
	{
		auto &Entry = mp_Entries[_Key];
		Entry.f_Update(co_await fg_Suspend());
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: 'Entry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
		co_return 0;
	}

	TCFuture<int> f_StateAlias()
	{
		auto &State = mp_State;
		auto Key = fg_RandomID(State.mp_Items);
		auto &Entry = mp_State.mp_Items[Key];
		co_await fg_Suspend();
		int Result = Entry;
		mp_State.mp_Items.f_Remove(Key);
		co_return Result;
	}
};

// A member function of a member of an entry, and an operator of it, called
// after an await in their arguments, use it after the suspension.
struct CRound63Value
{
	int m_Value = 0;
	int f_Read(int _Offset) const
	{
		return m_Value + _Offset;
	}
	int operator()(int _Offset) const
	{
		return m_Value + _Offset;
	}
};
struct CRound63Entry
{
	CRound63Value m_Value;
};
struct CRound63Actor : CActor
{
	TCMap<int, CRound63Entry> mp_Items;
	TCMap<int, CRound63Value> mp_Values;

	TCFuture<int> f_Member()
	{
		auto &Entry = mp_Items[1];
		co_return Entry.m_Value.f_Read(co_await fg_Suspend());
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'Entry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
	}

	TCFuture<int> f_Operator()
	{
		auto &Value = mp_Values[1];
		co_return Value(co_await fg_Suspend());
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'Value' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
	}
};

// A removal through a pointer pointed at the collection again removes from
// it, an iterator from a free `begin` is a lookup, a value a member function
// gives is its own, and a comparison whose result is ignored doesn't
// re-validate.
namespace NRound64
{
	template <typename t_CContainer>
	auto begin(t_CContainer &&_Items) -> decltype(_Items.f_GetIterator());
}
struct CRound64Map : TCMap<int, int>
{
	int f_GetLen() const;
};
struct CRound64Actor : CPhaseActor
{
	DMibActorEntries(Destroy, Owner) TCMap<CStr, int> mp_Items;
	TCMap<int, int> mp_Values;
	CRound64Map mp_Counted;

	TCFuture<int> f_RemovedThroughPointer()
	{
		auto Guard = co_await f_CheckDestroyedOnResume();
		auto Key = fg_RandomID(mp_Items);
		auto &Entry = mp_Items[Key];
		auto *pItems = &mp_Items;
		auto *pSaved = pItems;
		pItems = pSaved;
		pItems->f_Remove(Key);
		co_await fg_Suspend();
		co_return Entry;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'Entry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
	}

	TCFuture<int> f_FreeBegin()
	{
		auto iEntry = NRound64::begin(mp_Values);
		co_await fg_Suspend();
		co_return *iEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: 'iEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
	}

	TCFuture<int> f_Count()
	{
		int const &Count = mp_Counted.f_GetLen();
		co_await fg_Suspend();
		co_return Count;
	}

	TCFuture<int> f_Ignored()
	{
		int *pValue = mp_Values.f_FindEqual(1);
		auto OnResume = co_await fg_OnResume
			(
				[&]() -> CExceptionPointer
				{
					bool bIgnored = pValue != mp_Values.f_FindEqual(1) ? true : false;
					return nullptr;
				}
			)
		;
		co_await fg_Suspend();
		co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: 'pValue' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
	}
};

// What a call whose arguments suspend is given by reference, and a lookup it
// is made on as it is, are used after the suspension; the same element of an
// array of states holds the same collection; a swap gives what a pointer holds
// to the other; and the address of a local pointer isn't a lookup.
int fg_Round65Read(int const &_Value, int _Other);
template <typename t_CType>
void fg_Swap(t_CType &o_Left, t_CType &o_Right);
struct CRound65Value
{
	int m_Value = 0;
	int f_Read(int _Offset);
};
struct CRound65State
{
	DMibActorEntries(Owner) TCMap<CStr, int> m_Items;
};
struct CRound65Actor : CActor
{
	TCMap<int, int> mp_Items;
	TCMap<int, CRound65Value> mp_Values;
	CRound65State mp_States[2];

	TCFuture<int> f_Argument()
	{
		auto &Entry = mp_Items[1];
		co_return fg_Round65Read(Entry, co_await fg_Suspend());
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'Entry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
	}

	TCFuture<int> f_DirectLookup()
	{
		co_return mp_Values[0].f_Read(co_await fg_Suspend());
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: the data of the actor that this call uses is looked up before its arguments suspend, during which other calls of the actor can change or erase the data; look it up after the suspension [malterlib-coroutine-actor-reference]
	}

	TCFuture<int> f_Assigned()
	{
		mp_Values[0] = CRound65Value{co_await fg_Suspend()};
		co_return 0;
	}

	TCFuture<int> f_ArrayElement()
	{
		auto Key = fg_RandomID(mp_States[0].m_Items);
		auto &Entry = mp_States[0].m_Items[Key];
		co_await fg_Suspend();
		int Value = Entry;
		mp_States[0].m_Items.f_Remove(Key);
		co_return Value;
	}

	TCFuture<int> f_Swapped()
	{
		auto *pEntry = mp_Values.f_FindEqual(0);
		CRound65Value *pOther = nullptr;
		fg_Swap(pEntry, pOther);
		co_await fg_Suspend();
		co_return pOther->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'pOther' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
	}

	TCFuture<int> f_Address()
	{
		CRound65Value *pEntry = mp_Values.f_FindEqual(0);
		CRound65Value **ppEntry = &pEntry;
		auto OnResume = co_await fg_OnResume
			(
				[&]() -> CExceptionPointer
				{
					pEntry = mp_Values.f_FindEqual(0);
					if (!pEntry)
						return fg_MakeError("Gone");
					return {};
				}
			)
		;
		co_await fg_Suspend();
		co_return (*ppEntry)->m_Value;
	}
};

// A comparison that only one value of a conditional makes, where the other
// succeeds, doesn't re-validate; and a pointer kept from before, also through
// a cast or arithmetic, isn't a fresh lookup.
struct CRound66Actor : CActor
{
	TCMap<int, int> mp_Items;

	TCFuture<int> f_Conditional(bool _bValidate)
	{
		int *pEntry = mp_Items.f_FindEqual(1);
		auto OnResume = co_await fg_OnResume
			(
				[&]() -> CExceptionPointer
				{
					return _bValidate ? (pEntry == mp_Items.f_FindEqual(1) ? CExceptionPointer{} : fg_MakeError("Gone")) : CExceptionPointer{};
				}
			)
		;
		co_await fg_Suspend();
		co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
	}

	TCFuture<int> f_ConditionalError(bool _bValidate)
	{
		int *pEntry = mp_Items.f_FindEqual(1);
		auto OnResume = co_await fg_OnResume
			(
				[&]() -> CExceptionPointer
				{
					return _bValidate ? (pEntry == mp_Items.f_FindEqual(1) ? CExceptionPointer{} : fg_MakeError("Gone")) : fg_MakeError("Not validated");
				}
			)
		;
		co_await fg_Suspend();
		co_return *pEntry;
	}

	TCFuture<int> f_ParenthesizedCondition()
	{
		int *pEntry = mp_Items.f_FindEqual(1);
		auto OnResume = co_await fg_OnResume
			(
				[&]() -> CExceptionPointer
				{
					return (pEntry != mp_Items.f_FindEqual(1)) ? fg_MakeError("Gone") : CExceptionPointer{};
				}
			)
		;
		co_await fg_Suspend();
		co_return *pEntry;
	}

	TCFuture<int> f_Cast()
	{
		int *pEntry = mp_Items.f_FindEqual(1);
		int *pSaved = pEntry;
		auto OnResume = co_await fg_OnResume
			(
				[&]() -> CExceptionPointer
				{
					pEntry = static_cast<int *>(pSaved);
					return {};
				}
			)
		;
		co_await fg_Suspend();
		co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
	}

	TCFuture<int> f_Arithmetic()
	{
		int *pEntry = mp_Items.f_FindEqual(1);
		int *pSaved = pEntry;
		auto OnResume = co_await fg_OnResume
			(
				[&]() -> CExceptionPointer
				{
					pEntry = pSaved + 0;
					return {};
				}
			)
		;
		co_await fg_Suspend();
		co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
	}
};

// A later callback that starts a phase through a pointer to a member
// function, after the guard checked it, can remove the entries; one that
// starts a phase of another actor doesn't remove these.
struct CRound68App : CDistributedAppActor
{
	bool m_bStopNow = false;
	DMibActorEntries(StopApp) TCMap<CStr, int> mp_Items;

	DMibActorPhaseEntry(StopApp) void f_StopNow()
	{
		mp_State.m_bStoppingApp = true;
		mp_Items.f_Clear();
	}

	TCFuture<int> f_Indirect()
	{
		auto Guard = co_await fp_CheckStoppedOrDestroyedOnResume();
		auto fStop = &CRound68App::f_StopNow;
		auto Later = co_await fg_OnResume
			(
				[this, fStop]() -> CExceptionPointer
				{
					if (m_bStopNow)
						(this->*fStop)();
					return {};
				}
			)
		;
		auto &Entry = mp_Items["key"];
		co_await fg_Suspend();
		co_return Entry;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'Entry' refers into the data of the actor and is used after a suspension; 'mp_Items' loses entries only in StopApp, so a guard of that phase that is alive at the use, like fp_CheckStoppedOrDestroyedOnResume, makes it safe [malterlib-coroutine-actor-reference]
	}

	TCFuture<int> f_Other(CRound68App *_pOther)
	{
		auto Guard = co_await fp_CheckStoppedOrDestroyedOnResume();
		auto Later = co_await fg_OnResume
			(
				[_pOther]() -> CExceptionPointer
				{
					_pOther->f_StopApp();
					return {};
				}
			)
		;
		auto &Entry = mp_Items["key"];
		co_await fg_Suspend();
		co_return Entry;
	}
};

// A pointer pointed at a local, then offset, cast or chosen, points into the
// local.
struct CRound68Actor : CActor
{
	TCMap<int, int> mp_Items;

	TCFuture<int> f_Offset()
	{
		int Local[2] = {};
		int *pValue = mp_Items.f_FindEqual(0);
		pValue = Local;
		int *pNext = pValue + 1;
		int *pCast = static_cast<int *>(pValue);
		int *pChosen = pNext ? pNext : pCast;
		co_await fg_Suspend();
		co_return *pNext + *pCast + *pChosen;
	}
};

// A pointer copied through a reference to the pointer member of an entry
// points to an object of its own; a free helper given the object can start a
// phase; a free `begin` can give a pointer into the data; and either value of
// a conditional of the same collection, or the last of a comma, is a lookup
// of it.
struct CRound69Entry
{
	int *m_pValue;
};
struct CRound69Actor;
void fg_Round69Stop(CRound69Actor *_pThis);
namespace NMib::NContainer
{
	int *begin(TCVector<int> &_Vector);
}
struct CRound69Actor : CDistributedAppActor
{
	DMibActorEntries(Destroy) TCMap<int, CRound69Entry> mp_Entries;
	DMibActorEntries(StopApp) TCMap<int, int> mp_Items;
	TCVector<int> mp_Values;

	DMibActorPhaseEntry(StopApp) void f_StopNow()
	{
		mp_State.m_bStoppingApp = true;
		mp_Items.f_Clear();
	}

	TCFuture<int> f_PointerMember()
	{
		auto Guard = co_await f_CheckDestroyedOnResume();
		auto &pMember = mp_Entries[1].m_pValue;
		int *pValue = pMember;
		co_await fg_Suspend();
		co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: 'pValue' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
	}

	TCFuture<int> f_FreeHelper()
	{
		auto Guard = co_await fp_CheckStoppedOrDestroyedOnResume();
		auto &Entry = mp_Items[1];
		co_await fg_Suspend();
		fg_Round69Stop(this);
		co_return Entry;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'Entry' refers into the data of the actor and is used after a suspension; 'mp_Items' loses entries only in StopApp, so a guard of that phase that is alive at the use, like fp_CheckStoppedOrDestroyedOnResume, makes it safe [malterlib-coroutine-actor-reference]
	}

	TCFuture<int> f_Begin()
	{
		auto *pValue = begin(mp_Values);
		co_await fg_Suspend();
		co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: 'pValue' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
	}

	TCFuture<int> f_Conditional(bool _bFirst)
	{
		auto Guard = co_await fp_CheckStoppedOrDestroyedOnResume();
		auto &Entry = _bFirst ? mp_Items[1] : mp_Items[2];
		auto &Last = (void(0), mp_Items[1]);
		co_await fg_Suspend();
		co_return Entry + Last;
	}
};
void fg_Round69Stop(CRound69Actor *_pThis)
{
	_pThis->f_StopNow();
}

// A call through a pointer to a function, or a constructor, whose other
// arguments suspend uses what it is given by reference after them.
struct CRound69Copy
{
	int m_Value;
	CRound69Copy(int const &_Value, int _Extra)
		: m_Value(_Value + _Extra)
	{
	}
};
struct CRound69Calls : CActor
{
	TCMap<int, int> mp_Items;

	TCFuture<int> f_Pointer(void (*_fUse)(int &_Value, int _Extra))
	{
		int &Entry = mp_Items[0];
		_fUse(Entry, co_await fg_Suspend());
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: 'Entry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
		co_return 0;
	}

	TCFuture<int> f_Construct()
	{
		CRound69Copy Copy{mp_Items[0], co_await fg_Suspend()};
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: the data of the actor that this call uses is looked up before its arguments suspend, during which other calls of the actor can change or erase the data; look it up after the suspension [malterlib-coroutine-actor-reference]
		co_return Copy.m_Value;
	}
};

// A function whose returns all succeed doesn't make an error, whatever its
// name; and a pointer moved from a lookup is the lookup.
CExceptionPointer fg_Round70GetError()
{
	return {};
}
struct CRound70Actor : CDistributedAppActor
{
	DMibActorEntries(StopApp) TCMap<int, int> mp_Items;
	TCMap<int, int> mp_Values;

	TCFuture<int> f_NamedError()
	{
		auto Guard = co_await fg_OnResume
			(
				[this]() -> CExceptionPointer
				{
					if (mp_State.m_bStoppingApp)
						return fg_Round70GetError();
					return {};
				}
			)
		;
		auto &Entry = mp_Items[1];
		co_await fg_Suspend();
		co_return Entry;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'Entry' refers into the data of the actor and is used after a suspension; 'mp_Items' loses entries only in StopApp, so a guard of that phase that is alive at the use, like fp_CheckStoppedOrDestroyedOnResume, makes it safe [malterlib-coroutine-actor-reference]
	}

	TCFuture<int> f_Moved()
	{
		int *pValue = mp_Values.f_FindEqual(1);
		int *pMoved = fg_Move(pValue);
		int *pDirect = fg_Move(mp_Values.f_FindEqual(1));
		co_await fg_Suspend();
		co_return *pMoved + *pDirect;
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: 'pMoved' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
// CHECK-MESSAGES: :[[@LINE-2]]:24: warning: 'pDirect' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
	}
};

// A hierarchy that shares virtual bases is looked at once per base, not once
// per path to it, which is exponential in its depth.
template <int t_Depth>
struct TCRound71Diamond;
template <int t_Depth, int t_Tag>
struct TCRound71Leg : virtual TCRound71Diamond<t_Depth - 1>
{
	~TCRound71Leg() noexcept;
};
template <int t_Depth>
struct TCRound71Diamond : TCRound71Leg<t_Depth, 0>, TCRound71Leg<t_Depth, 1>
{
	~TCRound71Diamond() noexcept;
};
template <>
struct TCRound71Diamond<0>
{
	~TCRound71Diamond() noexcept;
};
TCRound71Diamond<40> fg_Round71Make();
struct CRound71Actor : CActor
{
	TCFuture<int> f_Deep()
	{
		auto Deep = fg_Round71Make();
		co_return 0;
	}
};

// An element of an array in an entry, or the entry offset, is in the entry;
// and what an assignment assigns is what the value is.
struct CRound71Entry
{
	int m_Values[4];
};
struct CRound71Phases : CPhaseActor
{
	DMibActorEntries(Destroy) TCMap<int, CRound71Entry> mp_Items;
	TCMap<int, int> mp_Values;

	TCFuture<int> f_Element()
	{
		auto Guard = co_await f_CheckDestroyedOnResume();
		auto &Value = mp_Items[1].m_Values[0];
		auto *pEntry = &mp_Items[1] + 0;
		co_await fg_Suspend();
		co_return Value + pEntry->m_Values[0];
	}

	TCFuture<int> f_Chained()
	{
		int *pFirst = nullptr;
		int *pSecond = nullptr;
		pSecond = (pFirst = mp_Values.f_FindEqual(0));
		co_await fg_Suspend();
		co_return *pSecond;
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: 'pSecond' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
	}
};

// A long coroutine is gone through about once, not once per statement in it,
// and what is told of its locals is told once.
#define DRound72Step(d_Index) \
	auto &Entry##d_Index = mp_Items[d_Index]; \
	co_await fg_Suspend(); \
	Value += Entry##d_Index;
#define DRound72Ten(d_Tens) \
	DRound72Step(d_Tens##0) DRound72Step(d_Tens##1) DRound72Step(d_Tens##2) DRound72Step(d_Tens##3) DRound72Step(d_Tens##4) \
	DRound72Step(d_Tens##5) DRound72Step(d_Tens##6) DRound72Step(d_Tens##7) DRound72Step(d_Tens##8) DRound72Step(d_Tens##9)
struct CRound72Long : CPhaseActor
{
	DMibActorEntries(Destroy) TCMap<int, int> mp_Items;

	TCFuture<int> f_Long()
	{
		auto Guard = co_await f_CheckDestroyedOnResume();
		int Value = 0;
		DRound72Ten(1) DRound72Ten(2) DRound72Ten(3) DRound72Ten(4) DRound72Ten(5)
		DRound72Ten(6) DRound72Ten(7) DRound72Ten(8) DRound72Ten(9) DRound72Ten(10)
		DRound72Ten(11) DRound72Ten(12) DRound72Ten(13) DRound72Ten(14) DRound72Ten(15)
		DRound72Ten(16) DRound72Ten(17) DRound72Ten(18) DRound72Ten(19) DRound72Ten(20)
		co_return Value;
	}
};

// Given to a helper whose body doesn't change it, a pointer isn't looked up
// again.
void fg_Round73NoOp(int *&_pEntry)
{
}
void fg_Round73Refresh(int *&_pEntry)
{
	_pEntry = nullptr;
}
struct CRound73Helper : CPhaseActor
{
	TCMap<int, int> mp_Items;

	TCFuture<int> f_NoOp()
	{
		auto *pEntry = mp_Items.f_FindEqual(1);
		auto OnResume = co_await fg_OnResume
			(
				[&pEntry]() -> CExceptionPointer
				{
					fg_Round73NoOp(pEntry);
					return {};
				}
			)
		;
		co_await fg_Suspend();
		co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
	}

	TCFuture<int> f_Refresh()
	{
		auto *pEntry = mp_Items.f_FindEqual(1);
		auto OnResume = co_await fg_OnResume
			(
				[&pEntry]() -> CExceptionPointer
				{
					fg_Round73Refresh(pEntry);
					return {};
				}
			)
		;
		co_await fg_Suspend();
		co_return pEntry ? *pEntry : 0;
	}
};

// An actor derived from deeply, which is another actor whatever the depth.
template <int t_Depth>
struct TCRound73Deep : TCRound73Deep<t_Depth - 1>
{
};
template <>
struct TCRound73Deep<0> : CPhaseActor
{
};
struct CRound73Deep : CPhaseActor
{
	TCSharedPointer<TCRound73Deep<20>> mp_pOther;
	DMibActorEntries(Destroy) TCMap<int, int> mp_Items;

	TCFuture<int> f_Run()
	{
		auto Guard = co_await mp_pOther->f_CheckDestroyedOnResume();
		auto &Entry = mp_Items[1];
		co_await fg_Suspend();
		co_return Entry;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'Entry' refers into the data of the actor and is used after a suspension; 'mp_Items' loses entries only in Destroy, so a guard of that phase that is alive at the use, like f_CheckDestroyedOnResume, makes it safe [malterlib-coroutine-actor-reference]
	}
};

// What the callback calls only in a branch an `if constexpr` discards, also
// in a helper, doesn't run.
struct CRound73Discarded : CDistributedAppActor
{
	DMibActorEntries(StopApp) TCMap<int, int> mp_Items;

	void f_StopWrapper()
	{
		if constexpr (false)
			f_StopApp();
	}

	TCFuture<int> f_Run()
	{
		auto Guard = co_await fg_OnResume
			(
				[this]() -> CExceptionPointer
				{
					if (mp_State.m_bStoppingApp)
						return fg_MakeError("stopped");
					if constexpr (false)
						f_StopApp();
					f_StopWrapper();
					return {};
				}
			)
		;
		auto &Entry = mp_Items[1];
		co_await fg_Suspend();
		co_return Entry;
	}
};

// A state class reached through many paths of shared bases is looked into
// once.
template <int t_Depth>
struct TCRound73State;
template <int t_Depth, int t_Index>
struct TCRound73Edge : virtual TCRound73State<t_Depth - 1>
{
};
template <int t_Depth>
struct TCRound73State
	: TCRound73Edge<t_Depth, 0>
	, TCRound73Edge<t_Depth, 1>
	, TCRound73Edge<t_Depth, 2>
	, TCRound73Edge<t_Depth, 3>
	, TCRound73Edge<t_Depth, 4>
	, TCRound73Edge<t_Depth, 5>
	, TCRound73Edge<t_Depth, 6>
	, TCRound73Edge<t_Depth, 7>
{
};
template <>
struct TCRound73State<0>
{
	DMibActorPhaseFlag(Destroy) bool m_bDestroyed;
};
struct CRound73Shared : CPhaseActor
{
	TCSharedPointer<TCRound73State<10>> mp_pState;
	DMibActorEntries(Destroy) TCMap<int, int> mp_Items;

	TCFuture<int> f_Run()
	{
		auto Guard = co_await fg_OnResume
			(
				[this]() -> CExceptionPointer
				{
					if (mp_pState->m_bDestroyed)
						return fg_MakeError("gone");
					return {};
				}
			)
		;
		auto &Entry = mp_Items[1];
		co_await fg_Suspend();
		co_return Entry;
	}
};

// A callback that starts a phase through what it constructs, a helper after
// many others, or that returns success only in a discarded branch.
template <int t_Depth>
void fg_Round75Nop()
{
	if constexpr (t_Depth > 0)
		fg_Round75Nop<t_Depth - 1>();
}
struct CRound75Callbacks : CDistributedAppActor
{
	DMibActorEntries(StopApp) TCMap<int, int> mp_Items;
	bool m_bStopNow = false;

	DMibActorPhaseEntry(StopApp) void f_StopNow()
	{
		mp_State.m_bStoppingApp = true;
		mp_Items.f_Clear();
	}

	struct CStop
	{
		CStop(CRound75Callbacks *_pThis)
		{
			_pThis->f_StopNow();
		}
	};

	void f_StopWrapper()
	{
		f_StopNow();
	}

	TCFuture<int> f_Construct()
	{
		auto Guard = co_await fp_CheckStoppedOrDestroyedOnResume();
		auto Later = co_await fg_OnResume
			(
				[this]() -> CExceptionPointer
				{
					if (m_bStopNow)
						CStop Stop(this);
					return {};
				}
			)
		;
		auto &Entry = mp_Items[1];
		co_await fg_Suspend();
		co_return Entry;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'Entry' refers into the data of the actor and is used after a suspension; 'mp_Items' loses entries only in StopApp, so a guard of that phase that is alive at the use, like fp_CheckStoppedOrDestroyedOnResume, makes it safe [malterlib-coroutine-actor-reference]
	}

	TCFuture<int> f_ManyHelpers()
	{
		auto Guard = co_await fg_OnResume
			(
				[this]() -> CExceptionPointer
				{
					if (mp_State.m_bStoppingApp)
						return fg_MakeError("stopping");
					fg_Round75Nop<20>();
					if (m_bStopNow)
						f_StopWrapper();
					return {};
				}
			)
		;
		auto &Entry = mp_Items[1];
		co_await fg_Suspend();
		co_return Entry;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'Entry' refers into the data of the actor and is used after a suspension; 'mp_Items' loses entries only in StopApp, so a guard of that phase that is alive at the use, like fp_CheckStoppedOrDestroyedOnResume, makes it safe [malterlib-coroutine-actor-reference]
	}
};
struct CRound75Discarded : CPhaseActor
{
	DMibActorEntries(Destroy) TCMap<int, int> mp_Items;

	TCFuture<int> f_Run()
	{
		auto Guard = co_await fg_OnResume
			(
				[this]() -> CExceptionPointer
				{
					if constexpr (false)
						return {};
					if (f_IsDestroyed())
						return fg_MakeError("destroyed");
					return {};
				}
			)
		;
		auto &Entry = mp_Items[1];
		co_await fg_Suspend();
		co_return Entry;
	}
};

// A removal that doesn't run doesn't remove what this call owns, and a
// reference bound to a copy of a bit-field is to the copy.
struct CRound75Entry
{
	unsigned m_Bits : 3;
};
struct CRound75Owner : CActor
{
	DMibActorEntries(Owner) TCMap<CStr, int> mp_Items;
	TCMap<int, CRound75Entry> mp_Entries;

	TCFuture<int> f_Discarded()
	{
		auto Key = fg_RandomID(mp_Items);
		auto &Value = mp_Items[Key];
		if constexpr (false)
			mp_Items.f_Remove(Key);
		(void)sizeof((mp_Items.f_Remove(Key), 0));
		co_await fg_Suspend();
		co_return Value;
	}

	TCFuture<int> f_BitField()
	{
		auto const &Bits = mp_Entries[1].m_Bits;
		co_await fg_Suspend();
		co_return Bits;
	}
};

// A later callback that starts a phase through a pointer to a function, or
// removes entries, and a fresh lookup with a key given by reference.
struct CRound76App;
void fg_Round76Stop(CRound76App *_pThis);
struct CRound76App : CDistributedAppActor
{
	bool mp_bStopNow = false;
	bool mp_bClear = false;
	DMibActorEntries(StopApp) TCMap<int, int> mp_Items;
	TCMap<int, int> mp_Plain;

	TCFuture<int> f_Pointer()
	{
		auto Guard = co_await fp_CheckStoppedOrDestroyedOnResume();
		auto fStop = &fg_Round76Stop;
		auto Later = co_await fg_OnResume
			(
				[this, fStop]() -> CExceptionPointer
				{
					if (mp_bStopNow)
						fStop(this);
					return {};
				}
			)
		;
		auto &Entry = mp_Items[0];
		co_await fg_Suspend();
		co_return Entry;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'Entry' refers into the data of the actor and is used after a suspension; 'mp_Items' loses entries only in StopApp, so a guard of that phase that is alive at the use, like fp_CheckStoppedOrDestroyedOnResume, makes it safe [malterlib-coroutine-actor-reference]
	}

	TCFuture<int> f_RemovesAfter(int _Key)
	{
		int *pEntry = mp_Plain.f_FindEqual(_Key);
		auto Refresh = co_await fg_OnResume
			(
				[&]() -> CExceptionPointer
				{
					pEntry = mp_Plain.f_FindEqual(_Key);
					if (mp_bClear)
						mp_Plain.f_Clear();
					if (!pEntry)
						return fg_MakeError("Removed");
					return {};
				}
			)
		;
		co_await fg_Suspend();
		co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
	}

	TCFuture<int> f_LiteralKey()
	{
		int *pEntry = mp_Plain.f_FindEqual(0);
		auto Refresh = co_await fg_OnResume
			(
				[&]() -> CExceptionPointer
				{
					pEntry = mp_Plain.f_FindEqual(0);
					if (!pEntry)
						return fg_MakeError("Removed");
					return {};
				}
			)
		;
		co_await fg_Suspend();
		co_return *pEntry;
	}
};
void fg_Round76Stop(CRound76App *_pThis)
{
	_pThis->f_StopApp();
}

// An error of a helper whose success is only in a discarded branch, and a
// lookup used as it is by a call whose arguments suspend, into what loses no
// entries or in a discarded branch.
CExceptionPointer fg_Round76Error()
{
	if constexpr (false)
		return {};
	return fg_MakeError("stopping");
}
struct CRound76Entry
{
	int f_Read(int _Value) const;
};
struct CRound76Guarded : CDistributedAppActor
{
	DMibActorEntries(StopApp) TCMap<int, int> mp_Items;
	DMibActorEntries() TCMap<int, CRound76Entry> mp_Kept;
	TCMap<int, CRound76Entry> mp_Plain;

	TCFuture<int> f_Helper()
	{
		auto Guard = co_await fg_OnResume
			(
				[this]() -> CExceptionPointer
				{
					if (mp_State.m_bStoppingApp)
						return fg_Round76Error();
					return {};
				}
			)
		;
		int &Value = mp_Items[1];
		co_await fg_Suspend();
		co_return Value;
	}

	TCFuture<int> f_Inline()
	{
		if constexpr (false)
			co_return mp_Plain[0].f_Read(co_await fg_Suspend());
		co_return mp_Kept[0].f_Read(co_await fg_Suspend());
	}
};

// An error returned through an unchanged local, or a result that is set,
// ends the coroutine on resume.
struct CRound77Guarded : CDistributedAppActor
{
	DMibActorEntries(StopApp) TCMap<int, int> mp_Items;

	TCFuture<int> f_ErrorLocal()
	{
		int &Value = mp_Items[1];
		auto Guard = co_await fg_OnResume
			(
				[this]() -> CExceptionPointer
				{
					if (mp_State.m_bStoppingApp)
					{
						auto Error = fg_MakeError("Stopping");
						return Error;
					}
					return {};
				}
			)
		;
		co_await TCFuture<int>{};
		co_return Value;
	}

	TCFuture<int> f_SetResult()
	{
		int &Value = mp_Items[1];
		auto Guard = co_await fg_OnResume
			(
				[this]() -> TCAsyncResult<int>
				{
					if (mp_State.m_bStoppingApp)
					{
						TCAsyncResult<int> Result;
						Result.f_SetResult(0);
						return Result;
					}
					return {};
				}
			)
		;
		co_await TCFuture<int>{};
		co_return Value;
	}

	TCFuture<int> f_NotSet(bool _bSet)
	{
		int &Value = mp_Items[1];
		auto Guard = co_await fg_OnResume
			(
				[this, _bSet]() -> TCAsyncResult<int>
				{
					if (mp_State.m_bStoppingApp)
					{
						TCAsyncResult<int> Result;
						if (_bSet)
							Result.f_SetResult(0);
						return Result;
					}
					return {};
				}
			)
		;
		co_await TCFuture<int>{};
		co_return Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'Value' refers into the data of the actor and is used after a suspension; 'mp_Items' loses entries only in StopApp, so a guard of that phase that is alive at the use, like fp_CheckStoppedOrDestroyedOnResume, makes it safe [malterlib-coroutine-actor-reference]
	}
};

// Awaiting fg_OnResume runs its callback right away, which can start a phase,
// and an error helper is one however many helpers it goes through, but not
// one whose innermost helper returns success.
CExceptionPointer fg_Round78Success()
{
	return {};
}
CExceptionPointer fg_Round78Wrap1()
{
	return fg_Round78Success();
}
CExceptionPointer fg_Round78Wrap2()
{
	return fg_Round78Wrap1();
}
CExceptionPointer fg_Round78Wrap3()
{
	return fg_Round78Wrap2();
}
CExceptionPointer fg_Round78Wrap4()
{
	return fg_Round78Wrap3();
}
CExceptionPointer fg_Round78Error0()
{
	return fg_MakeError("Stopping");
}
CExceptionPointer fg_Round78Error1()
{
	return fg_Round78Error0();
}
CExceptionPointer fg_Round78Error2()
{
	return fg_Round78Error1();
}
CExceptionPointer fg_Round78Error3()
{
	return fg_Round78Error2();
}
CExceptionPointer fg_Round78Error4()
{
	return fg_Round78Error3();
}
struct CRound78App : CDistributedAppActor
{
	DMibActorEntries(StopApp) TCMap<int, int> mp_Items;

	TCFuture<int> f_StopsInCallback()
	{
		auto *pValue = mp_Items.f_FindEqual(1);
		auto OnResume = co_await fg_OnResume
			(
				[this]() -> CExceptionPointer
				{
					f_StopApp();
					return {};
				}
			)
		;
		co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: 'pValue' refers into the data of the actor and is used after a suspension; 'mp_Items' loses entries only in StopApp, so a guard of that phase that is alive at the use, like fp_CheckStoppedOrDestroyedOnResume, makes it safe [malterlib-coroutine-actor-reference]
	}

	TCFuture<int> f_SuccessHelper()
	{
		auto Guard = co_await fg_OnResume
			(
				[this]() -> CExceptionPointer
				{
					if (mp_State.m_bStoppingApp)
						return fg_Round78Wrap4();
					return {};
				}
			)
		;
		int *pValue = mp_Items.f_FindEqual(1);
		co_await fg_Suspend();
		co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: 'pValue' refers into the data of the actor and is used after a suspension; 'mp_Items' loses entries only in StopApp, so a guard of that phase that is alive at the use, like fp_CheckStoppedOrDestroyedOnResume, makes it safe [malterlib-coroutine-actor-reference]
	}

	TCFuture<int> f_ErrorHelper()
	{
		auto Guard = co_await fg_OnResume
			(
				[this]() -> CExceptionPointer
				{
					if (mp_State.m_bStoppingApp)
						return fg_Round78Error4();
					return {};
				}
			)
		;
		int *pValue = mp_Items.f_FindEqual(1);
		co_await fg_Suspend();
		co_return *pValue;
	}
};

// A guard mentioned only where it isn't evaluated still guards, also an entry
// referred to through a cast or a pointer to a member, and error helpers
// called by several are told once each.
struct CRound79Entry
{
	int m_Value;
};
#define DRound79Helper(d_N, d_P) \
	CExceptionPointer fg_Round79Error##d_N(bool _bValue) \
	{ \
		if (_bValue) \
			return fg_Round79Error##d_P(_bValue); \
		return fg_Round79Error##d_P(_bValue); \
	}
CExceptionPointer fg_Round79Error0(bool _bValue)
{
	return fg_MakeError("Error");
}
DRound79Helper(1, 0) DRound79Helper(2, 1) DRound79Helper(3, 2) DRound79Helper(4, 3)
DRound79Helper(5, 4) DRound79Helper(6, 5) DRound79Helper(7, 6) DRound79Helper(8, 7)
DRound79Helper(9, 8) DRound79Helper(10, 9) DRound79Helper(11, 10) DRound79Helper(12, 11)
DRound79Helper(13, 12) DRound79Helper(14, 13) DRound79Helper(15, 14) DRound79Helper(16, 15)
DRound79Helper(17, 16) DRound79Helper(18, 17) DRound79Helper(19, 18) DRound79Helper(20, 19)
struct CRound79Guarded : CPhaseActor
{
	DMibActorEntries(Destroy) TCMap<int, CRound79Entry> mp_Items;

	TCFuture<int> f_Sizeof()
	{
		auto Guard = co_await f_CheckDestroyedOnResume();
		(void)sizeof(Guard);
		int &Value = mp_Items[0].m_Value;
		co_await fg_Suspend();
		co_return Value;
	}

	TCFuture<int> f_MemberPointer()
	{
		auto Guard = co_await f_CheckDestroyedOnResume();
		int CRound79Entry::*pmValue = &CRound79Entry::m_Value;
		int &Value = mp_Items[0].*pmValue;
		int &Cast = static_cast<int &>(mp_Items[1].m_Value);
		co_await fg_Suspend();
		co_return Value + Cast;
	}

	TCFuture<int> f_Helpers(bool _bValue)
	{
		auto Guard = co_await fg_OnResume
			(
				[this, _bValue]() -> CExceptionPointer
				{
					if (f_IsDestroyed())
						return fg_Round79Error20(_bValue);
					return {};
				}
			)
		;
		int &Value = mp_Items[0].m_Value;
		co_await fg_Suspend();
		co_return Value;
	}
};

// A constructor given the object can start a phase, a callback reads what it
// doesn't re-validate on every resume, unless a guard of a phase alive before
// it protects it, and a comparison re-validates only when a mismatch ends the
// coroutine.
void fg_Round80Read(int _Value);
struct CRound80App : CDistributedAppActor
{
	DMibActorEntries(StopApp) TCMap<int, int> mp_Items;
	TCMap<int, int> mp_Plain;

	DMibActorPhaseEntry(StopApp) void f_StopNow()
	{
		mp_State.m_bStoppingApp = true;
		mp_Items.f_Clear();
	}

	struct CStop
	{
		CStop(CRound80App *_pThis)
		{
			_pThis->f_StopNow();
		}
	};

	TCFuture<int> f_Constructor()
	{
		auto Guard = co_await fp_CheckStoppedOrDestroyedOnResume();
		int &Entry = mp_Items[0];
		co_await fg_Suspend();
		CStop Stop(this);
		co_return Entry;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'Entry' refers into the data of the actor and is used after a suspension; 'mp_Items' loses entries only in StopApp, so a guard of that phase that is alive at the use, like fp_CheckStoppedOrDestroyedOnResume, makes it safe [malterlib-coroutine-actor-reference]
	}

	TCFuture<int> f_ReadInCallback()
	{
		int *pEntry = &mp_Plain[0];
		auto OnResume = co_await fg_OnResume
			(
				[&pEntry]() -> CExceptionPointer
				{
					fg_Round80Read(*pEntry);
// CHECK-MESSAGES: :[[@LINE-1]]:22: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
					return {};
				}
			)
		;
		co_await fg_Suspend();
		co_return 0;
	}

	TCFuture<int> f_GuardedCallback()
	{
		auto Guard = co_await fp_CheckStoppedOrDestroyedOnResume();
		int *pEntry = &mp_Items[0];
		auto OnResume = co_await fg_OnResume
			(
				[this, &pEntry]() -> CExceptionPointer
				{
					fg_Round80Read(*pEntry);
					if (pEntry != mp_Items.f_FindEqual(0))
						return fg_MakeError("Gone");
					return {};
				}
			)
		;
		co_await fg_Suspend();
		co_return *pEntry;
	}

	TCFuture<int> f_GoneSucceeds()
	{
		int *pEntry = &mp_Plain[0];
		auto OnResume = co_await fg_OnResume
			(
				[this, &pEntry]() -> CExceptionPointer
				{
					int *pFresh = mp_Plain.f_FindEqual(0);
					return pEntry == pFresh || !pFresh ? CExceptionPointer{} : fg_MakeError("Changed");
				}
			)
		;
		co_await fg_Suspend();
		co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
	}

	TCFuture<int> f_Inverted()
	{
		int *pEntry = mp_Plain.f_FindEqual(1);
		auto OnResume = co_await fg_OnResume
			(
				[&]() -> CExceptionPointer
				{
					return pEntry == mp_Plain.f_FindEqual(1) ? fg_MakeError("Equal") : CExceptionPointer{};
				}
			)
		;
		co_await fg_Suspend();
		co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
	}
};

// A scope exit or a destructor that starts a phase where its scope ends, a
// helper that stops another actor, and a call that a lambda doesn't run.
struct CRound81Stopper;
struct CRound81App : CDistributedAppActor
{
	DMibActorEntries(StopApp) TCMap<int, int> mp_Items;

	DMibActorPhase(StopApp) TCFuture<int> fp_StopApp() override
	{
		mp_Items.f_Clear();
		co_return 0;
	}

	static void fs_StopOther(CRound81App *_pThis)
	{
		_pThis->self / [_pThis] { return _pThis->f_StopApp(); };
	}

	TCFuture<int> f_ScopeExit()
	{
		auto Guard = co_await fp_CheckStoppedOrDestroyedOnResume();
		int &Entry = mp_Items[1];
		co_await fg_Suspend();
		{
			auto Cleanup = g_OnScopeExit / [this] { f_StopApp(); };
		}
		co_return Entry;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'Entry' refers into the data of the actor and is used after a suspension; 'mp_Items' loses entries only in StopApp, so a guard of that phase that is alive at the use, like fp_CheckStoppedOrDestroyedOnResume, makes it safe [malterlib-coroutine-actor-reference]
	}

	TCFuture<int> f_Destructor();

	TCFuture<int> f_StopsOther(CRound81App *_pOther)
	{
		auto Guard = co_await fp_CheckStoppedOrDestroyedOnResume();
		auto Callback = co_await fg_OnResume
			(
				[_pOther] -> CExceptionPointer
				{
					fs_StopOther(_pOther);
					return {};
				}
			)
		;
		int &Entry = mp_Items[1];
		co_await fg_Suspend();
		co_return Entry;
	}

	TCFuture<int> f_NotRun()
	{
		auto Guard = co_await fp_CheckStoppedOrDestroyedOnResume();
		int &Entry = mp_Items[1];
		auto fNothing = [this]
			{
				if constexpr (false)
					f_StopApp();
				(void)noexcept(f_StopApp());
			}
		;
		fNothing();
		co_await fg_Suspend();
		co_return Entry;
	}
};
struct CRound81Stopper
{
	CRound81App *m_pThis;
	~CRound81Stopper()
	{
		m_pThis->f_StopApp();
	}
};
TCFuture<int> CRound81App::f_Destructor()
{
	auto Guard = co_await fp_CheckStoppedOrDestroyedOnResume();
	int &Entry = mp_Items[1];
	co_await fg_Suspend();
	{
		CRound81Stopper Stopper{this};
	}
	co_return Entry;
// CHECK-MESSAGES: :[[@LINE-1]]:12: warning: 'Entry' refers into the data of the actor and is used after a suspension; 'mp_Items' loses entries only in StopApp, so a guard of that phase that is alive at the use, like fp_CheckStoppedOrDestroyedOnResume, makes it safe [malterlib-coroutine-actor-reference]
}

// Nested awaits in a helper that a callback of fg_OnResume calls are looked at
// once each, not once per path through the operands, which is exponential in
// their depth.
TCFuture<int> fg_Round82Work(int _Value);
#define DRound82Await(d_Value) co_await fg_Round82Work(d_Value)
#define DRound82Await4(d_Value) DRound82Await(DRound82Await(DRound82Await(DRound82Await(d_Value))))
#define DRound82Await32(d_Value) DRound82Await4(DRound82Await4(DRound82Await4(DRound82Await4(DRound82Await4(DRound82Await4(DRound82Await4(DRound82Await4(d_Value))))))))
struct CRound82Nested : CDistributedAppActor
{
	TCFuture<int> f_Notify()
	{
		co_return DRound82Await32(0);
	}

	TCFuture<int> f_Run()
	{
		auto Guard = co_await fg_OnResume
			(
				[this]() -> CExceptionPointer
				{
					if (mp_State.m_bStoppingApp)
						return fg_MakeError("Stopped");
					f_Notify();
					return {};
				}
			)
		;
		co_return 0;
	}
};

// A helper given another actor first is followed again when it is given the
// own one.
struct CRound82Helper : CDistributedAppActor
{
	bool m_bStopNow = false;
	DMibActorEntries(StopApp) TCMap<int, int> mp_Items;

	static void fs_Stop(CRound82Helper *_pThis)
	{
		_pThis->f_StopApp();
	}

	TCFuture<int> f_Run(CRound82Helper *_pOther)
	{
		auto Guard = co_await fg_OnResume
			(
				[this, _pOther]() -> CExceptionPointer
				{
					if (mp_State.m_bStoppingApp)
						return fg_MakeError("Stopped");
					if (m_bStopNow)
					{
						fs_Stop(_pOther);
						fs_Stop(this);
					}
					return {};
				}
			)
		;
		int *pEntry = mp_Items.f_FindEqual(1);
		co_await fg_Suspend();
		co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: 'pEntry' refers into the data of the actor and is used after a suspension; 'mp_Items' loses entries only in StopApp, so a guard of that phase that is alive at the use, like fp_CheckStoppedOrDestroyedOnResume, makes it safe [malterlib-coroutine-actor-reference]
	}

	TCFuture<int> fp_StopApp() override
	{
		mp_Items.f_Clear();
		co_return 0;
	}
};

// An error result made of itself, of a constant condition, and through many
// locals that are made of others is told once each.
struct CRound83Errors : CDistributedAppActor
{
	DMibActorEntries(StopApp) TCMap<int, int> mp_Items;

	TCFuture<int> f_Self()
	{
		auto Guard = co_await fg_OnResume
			(
				[this]() -> CExceptionPointer
				{
					CExceptionPointer pError = false ? pError : fg_MakeError("Stopped");
					if (mp_State.m_bStoppingApp)
						return pError;
					return {};
				}
			)
		;
		int &Value = mp_Items[0];
		co_await fg_Suspend();
		co_return Value;
	}

	TCFuture<int> f_Chain(bool _bChoose)
	{
		auto Guard = co_await fg_OnResume
			(
				[this, _bChoose]() -> CExceptionPointer
				{
					auto pError0 = fg_MakeError("Stopped");
					auto pError1 = _bChoose ? pError0 : pError0;
					auto pError2 = _bChoose ? pError1 : pError1;
					auto pError3 = _bChoose ? pError2 : pError2;
					auto pError4 = _bChoose ? pError3 : pError3;
					auto pError5 = _bChoose ? pError4 : pError4;
					auto pError6 = _bChoose ? pError5 : pError5;
					auto pError7 = _bChoose ? pError6 : pError6;
					auto pError8 = _bChoose ? pError7 : pError7;
					auto pError9 = _bChoose ? pError8 : pError8;
					auto pError10 = _bChoose ? pError9 : pError9;
					auto pError11 = _bChoose ? pError10 : pError10;
					auto pError12 = _bChoose ? pError11 : pError11;
					auto pError13 = _bChoose ? pError12 : pError12;
					auto pError14 = _bChoose ? pError13 : pError13;
					auto pError15 = _bChoose ? pError14 : pError14;
					auto pError16 = _bChoose ? pError15 : pError15;
					auto pError17 = _bChoose ? pError16 : pError16;
					auto pError18 = _bChoose ? pError17 : pError17;
					auto pError19 = _bChoose ? pError18 : pError18;
					auto pError20 = _bChoose ? pError19 : pError19;
					if (mp_State.m_bStoppingApp)
						return pError20;
					return {};
				}
			)
		;
		int &Value = mp_Items[0];
		co_await fg_Suspend();
		co_return Value;
	}
};

// A static function of a phase given another actor starts the phase of that
// one.
struct CRound83Other : CDistributedAppActor
{
	DMibActorEntries(StopApp) TCMap<int, int> mp_Items;

	DMibActorPhaseEntry(StopApp) static TCFuture<int> fs_Stop(TCActor<CRound83Other> _Other)
	{
		return _Other(&CRound83Other::f_StopApp);
	}

	TCFuture<int> f_Run(TCActor<CRound83Other> _Other)
	{
		auto Guard = co_await fp_CheckStoppedOrDestroyedOnResume();
		int &Value = mp_Items[0];
		co_await fs_Stop(_Other);
		co_return Value;
	}
};

// A saved comparison that is changed after doesn't re-validate, nor a fresh
// lookup that a cast to a reference assigns over, a guard given another
// actor's handle doesn't keep this one's entries, a holder given to what only
// looks at it stays alive, and helpers given others in many ways are followed
// a few times each.
struct CRound84Revalidate : CActor
{
	TCMap<int, int> mp_Items;

	TCFuture<int> f_Changed()
	{
		int *pEntry = mp_Items.f_FindEqual(0);
		auto Registration = co_await fg_OnResume
			(
				[this, &pEntry]() -> CExceptionPointer
				{
					bool bSame = pEntry == mp_Items.f_FindEqual(0);
					bSame = true;
					if (!bSame)
						return fg_MakeError("Entry gone");
					return {};
				}
			)
		;
		co_await fg_Suspend();
		co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
	}

	TCFuture<int> f_Restored()
	{
		int *pEntry = mp_Items.f_FindEqual(0);
		int *pSaved = pEntry;
		auto Registration = co_await fg_OnResume
			(
				[this, &pEntry, pSaved]() -> CExceptionPointer
				{
					pEntry = mp_Items.f_FindEqual(0);
					static_cast<int *&>(pEntry) = pSaved;
					return {};
				}
			)
		;
		co_await fg_Suspend();
		co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
	}
};
struct CRound84Guarded : CDistributedAppActor
{
	DMibActorEntries(StopApp) TCMap<int, int> mp_Items;

	DMibActorPhaseGuard(StopApp) static TCFuture<COnResumeScope> fs_Guard(TCActor<CRound84Guarded> _Actor);

	TCFuture<int> f_Other(TCActor<CRound84Guarded> _Other)
	{
		auto Registration = co_await fs_Guard(_Other);
		int *pEntry = mp_Items.f_FindEqual(0);
		co_await fg_Suspend();
		co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: 'pEntry' refers into the data of the actor and is used after a suspension; 'mp_Items' loses entries only in StopApp, so a guard of that phase that is alive at the use, like fp_CheckStoppedOrDestroyedOnResume, makes it safe [malterlib-coroutine-actor-reference]
	}
};
void fg_Round84Inspect(COnResumeScope const &_Guard) noexcept {}
struct CRound84Inspected : CPhaseActor
{
	DMibActorEntries(Destroy) TCMap<int, int> mp_Items;

	TCFuture<int> f_Inspected()
	{
		auto Guard = co_await f_CheckDestroyedOnResume();
		fg_Round84Inspect(Guard);
		int *pEntry = mp_Items.f_FindEqual(0);
		co_await fg_Suspend();
		co_return *pEntry;
	}
};
template <int t_Depth, typename... tp_CGiven>
void fg_Round84Branch(CPhaseActor *_pThis, CPhaseActor *_pOther, tp_CGiven... p_Given)
{
	if constexpr (t_Depth > 0)
	{
		fg_Round84Branch<t_Depth - 1>(_pThis, _pOther, p_Given..., _pThis);
		fg_Round84Branch<t_Depth - 1>(_pThis, _pOther, p_Given..., _pOther);
	}
}
struct CRound84Branches : CPhaseActor
{
	DMibActorEntries(Destroy) TCMap<int, int> mp_Items;

	TCFuture<int> f_Run(CPhaseActor *_pOther)
	{
		auto Guard = co_await fg_OnResume
			(
				[this, _pOther]() -> CExceptionPointer
				{
					if (f_IsDestroyed())
						return fg_MakeError("Destroyed");
					fg_Round84Branch<20>(this, _pOther);
					return {};
				}
			)
		;
		int *pEntry = mp_Items.f_FindEqual(0);
		co_await fg_Suspend();
		co_return *pEntry;
	}
};

// What a callback doesn't evaluate doesn't start a phase, nor move the key of
// an entry the coroutine owns.
struct CRound85Unevaluated : CDistributedAppActor
{
	DMibActorEntries(StopApp) TCMap<int, int> mp_Items;

	TCFuture<int> f_Callback()
	{
		auto Guard = co_await fp_CheckStoppedOrDestroyedOnResume();
		auto Callback = co_await fg_OnResume
			(
				[this]() -> CExceptionPointer
				{
					(void)noexcept(f_StopApp());
					return {};
				}
			)
		;
		int &Entry = mp_Items[1];
		co_await fg_Suspend();
		co_return Entry;
	}
};
struct CRound85Owned : CPhaseActor
{
	DMibActorEntries(Owner) TCMap<CStr, int> mp_Items;

	TCFuture<int> f_Owned()
	{
		auto Key = fg_RandomID(mp_Items);
		auto &Entry = mp_Items[Key];
		(void)sizeof(mp_Items[fg_Move(Key)]);
		co_await fg_Suspend();
		co_return Entry;
	}
};

// A key changed through a cast to a reference isn't the fresh key.
struct CRound86Key : CPhaseActor
{
	DMibActorEntries(Owner) TCMap<CStr, int> mp_Items;

	TCFuture<int> f_Add(CStr _OtherKey)
	{
		auto Key = fg_RandomID(mp_Items);
		mp_Items[Key] = 1;
		static_cast<CStr &>(Key) = _OtherKey;
		auto *pValue = mp_Items.f_FindEqual(Key);
		co_await fg_Suspend();
		int Result = *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:17: warning: 'pValue' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
		co_return Result;
	}
};

// A conditional registration re-validates only what every alternative does;
// a callback registered later, alive at the resume, can remove what an
// earlier one looked up again, also through a helper, but not in another
// collection; and a function called through a pointer can start a phase.
struct CRound87Revalidation : CPhaseActor
{
	TCMap<int, int> mp_Items;
	TCMap<int, int> mp_Other;

	void f_Tidy()
	{
		mp_Items.f_Clear();
	}

	TCFuture<int> f_Conditional(bool _bChoose)
	{
		auto *pValue = mp_Items.f_FindEqual(1);
		auto Guard = _bChoose
			? co_await fg_OnResume
			(
				[this, &pValue]() -> CExceptionPointer
				{
					pValue = mp_Items.f_FindEqual(1);
					if (!pValue)
						return fg_MakeError("Gone");
					return {};
				}
			)
			: co_await f_CheckDestroyedOnResume()
		;
		co_await fg_Suspend();
		co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: 'pValue' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
	}

	TCFuture<int> f_Helper()
	{
		auto *pValue = mp_Items.f_FindEqual(1);
		auto Lookup = co_await fg_OnResume
			(
				[this, &pValue]() -> CExceptionPointer
				{
					pValue = mp_Items.f_FindEqual(1);
					if (!pValue)
						return fg_MakeError("Gone");
					return {};
				}
			)
		;
		auto Cleanup = co_await fg_OnResume
			(
				[this]() -> CExceptionPointer
				{
					f_Tidy();
					return {};
				}
			)
		;
		co_await fg_Suspend();
		co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: 'pValue' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
	}

	TCFuture<int> f_SameCallback()
	{
		int *pValue = &mp_Items[1];
		auto Registration = co_await fg_OnResume
			(
				[&]() -> CExceptionPointer
				{
					pValue = &mp_Items[1];
					f_Tidy();
					return {};
				}
			)
		;
		co_await fg_Suspend();
		co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: 'pValue' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
	}

	TCFuture<int> f_OtherCollection()
	{
		auto *pValue = mp_Items.f_FindEqual(1);
		auto Lookup = co_await fg_OnResume
			(
				[this, &pValue]() -> CExceptionPointer
				{
					pValue = mp_Items.f_FindEqual(1);
					if (!pValue)
						return fg_MakeError("Gone");
					return {};
				}
			)
		;
		auto Cleanup = co_await fg_OnResume
			(
				[this]() -> CExceptionPointer
				{
					mp_Other.f_Clear();
					return {};
				}
			)
		;
		co_await fg_Suspend();
		co_return *pValue;
	}

	TCFuture<int> f_Expired()
	{
		int *pValue = &mp_Items[1];
		auto Refresh = co_await fg_OnResume
			(
				[&]() -> CExceptionPointer
				{
					pValue = &mp_Items[1];
					return {};
				}
			)
		;
		{
			auto Remove = co_await fg_OnResume
				(
					[&]() -> CExceptionPointer
					{
						mp_Items.f_Clear();
						return {};
					}
				)
			;
		}
		pValue = &mp_Items[1];
		co_await fg_Suspend();
		co_return *pValue;
	}

	TCFuture<int> f_Lambda()
	{
		int *pValue = nullptr;
		auto fInit = [&pValue, this] { pValue = mp_Items.f_FindEqual(0); };
		fInit();
		co_await fg_Suspend();
		co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: 'pValue' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
	}

	TCFuture<int> f_Clear()
	{
		int *pValue = mp_Items.f_FindEqual(0);
		auto fClear = [pValue]() mutable { pValue = nullptr; };
		co_await fg_Suspend();
		fClear();
		co_return 0;
	}
};

struct CRound87Stopping;
void fg_Round87Stop(CRound87Stopping *_pThis);
struct CRound87Stopping : CDistributedAppActor
{
	DMibActorEntries(StopApp) TCMap<int, int> mp_Items;

	TCFuture<int> fp_StopApp() override
	{
		mp_Items.f_Clear();
		co_return 0;
	}

	TCFuture<int> f_Stop()
	{
		auto Guard = co_await fp_CheckStoppedOrDestroyedOnResume();
		auto &Value = mp_Items[1];
		auto fStop = &fg_Round87Stop;
		fStop(this);
		co_return Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'Value' refers into the data of the actor and is used after a suspension; 'mp_Items' loses entries only in StopApp, so a guard of that phase that is alive at the use, like fp_CheckStoppedOrDestroyedOnResume, makes it safe [malterlib-coroutine-actor-reference]
	}
};
void fg_Round87Stop(CRound87Stopping *_pThis)
{
	_pThis->f_StopApp();
}

// A removal flag of another entry than the one a pointer points to now
// doesn't tell, a flag tested with another condition or in the other branch
// does, and a result cleared after it was set continues the coroutine.
struct CRound88Entry
{
	TCSharedPointer<bool> m_pDeleted;
	int m_Value;
};
struct CRound88Map
{
	CRound88Entry *f_FindEqual(int _Key);
	int *f_FindValue(int _Key);
};
struct CRound88Actor : CActor
{
	CRound88Map mp_Items;

	TCFuture<int> f_OtherEntry()
	{
		auto pEntry = mp_Items.f_FindEqual(1);
		auto pDeleted = pEntry->m_pDeleted;
		pEntry = mp_Items.f_FindEqual(2);
		auto fRead = [pEntry, pDeleted]
// CHECK-MESSAGES: :[[@LINE-1]]:17: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
			{
				if (*pDeleted)
					return 0;
				return pEntry->m_Value;
			}
		;
		co_await fg_Suspend();
		co_return fRead();
	}

	TCFuture<int> f_Or(bool _bSkip)
	{
		auto pEntry = mp_Items.f_FindEqual(1);
		auto pDeleted = pEntry->m_pDeleted;
		auto fRead = [pEntry, pDeleted, _bSkip]
			{
				if (*pDeleted || _bSkip)
					return 0;
				return pEntry->m_Value;
			}
		;
		auto fElse = [pEntry, pDeleted]
			{
				if (*pDeleted)
					return 0;
				else
					return pEntry->m_Value;
			}
		;
		co_await fg_Suspend();
		co_return fRead() + fElse();
	}

	TCFuture<int> f_Cleared(bool _bSkip)
	{
		int *pValue = mp_Items.f_FindValue(1);
		auto Guard = co_await fg_OnResume
			(
				[&]() -> TCAsyncResult<int>
				{
					if (_bSkip)
					{
						TCAsyncResult<int> Result;
						Result.f_SetResult(0);
						Result.f_Clear();
						return Result;
					}
					pValue = mp_Items.f_FindValue(1);
					return {};
				}
			)
		;
		co_await fg_Suspend();
		co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: 'pValue' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
	}
};

// A pointer pointed at the entry before the flag is copied from it is of the
// entry the flag tells about.
struct CRound88Pending
{
	TCSharedPointer<bool> m_pDeleted;
};
struct CRound88Pendings
{
	CRound88Pending &f_Insert();
	void f_Remove(CRound88Pending &_Pending);
};
struct CRound88Connector : CActor
{
	CRound88Pendings mp_Pending;

	TCFuture<int> f_Connect()
	{
		CRound88Pending *pPending;
		{
			CRound88Pending &Pending = mp_Pending.f_Insert();
			pPending = &Pending;
		}
		auto Cleanup = g_OnScopeExitActor / [this, pPendingDeleted = pPending->m_pDeleted, pPending]
			{
				if (*pPendingDeleted)
					return;
				mp_Pending.f_Remove(*pPending);
			}
		;
		co_await fg_Suspend();
		co_return 0;
	}
};

// A pointer an iterator gives is into the data too, and a static guard
// registers only for the call that first runs it.
struct CRound89Iterator : CPhaseActor
{
	TCMap<int, int> mp_Items;

	TCFuture<int> f_Arrow()
	{
		auto iEntry = mp_Items.f_GetIterator();
		int *pValue = iEntry.operator->();
		co_await fg_Suspend();
		co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: 'pValue' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
	}
};
struct CRound89Static : CDistributedAppActor
{
	DMibActorEntries(StopApp) TCMap<int, int> mp_Items;

	TCFuture<int> f_Static()
	{
		static auto Guard = co_await fp_CheckStoppedOrDestroyedOnResume();
		int *pValue = mp_Items.f_FindEqual(1);
		co_await fg_Suspend();
		co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: 'pValue' refers into the data of the actor and is used after a suspension; 'mp_Items' loses entries only in StopApp, so a guard of that phase that is alive at the use, like fp_CheckStoppedOrDestroyedOnResume, makes it safe [malterlib-coroutine-actor-reference]
	}

	TCFuture<int> fp_StopApp() override
	{
		mp_Items.f_Clear();
		co_return 0;
	}
};

// A function given a copy of the state, not the actor, can't stop it: a
// guard that reads only the copy doesn't keep the entry.
TCFuture<int> fg_Round90Suspend();
DMibActorPhaseFlag(StopApp)
bool fg_Round90IsStopping(CDistributedAppActor::CState const *_pState)
{
	return _pState->m_bStoppingApp;
}
struct CRound90Copied : CDistributedAppActor
{
	DMibActorEntries(StopApp) TCMap<int, int> mp_Items;

	TCFuture<int> f_Read()
	{
		auto State = mp_State;
		auto Guard = co_await fg_OnResume
			(
				[State]() -> CExceptionPointer
				{
					if (fg_Round90IsStopping(&State))
						return fg_MakeError("Stopped");
					return {};
				}
			)
		;
		auto &Entry = mp_Items[1];
		co_await fg_Round90Suspend();
		co_return Entry;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'Entry' refers into the data of the actor and is used after a suspension; 'mp_Items' loses entries only in StopApp, so a guard of that phase that is alive at the use, like fp_CheckStoppedOrDestroyedOnResume, makes it safe [malterlib-coroutine-actor-reference]
	}

	DMibActorPhase(StopApp)
	TCFuture<int> fp_StopApp() override
	{
		mp_Items.f_Clear();
		co_return 0;
	}
};

// A lambda that is made but never used doesn't run what it calls.
struct CRound90Unused : CDistributedAppActor
{
	DMibActorEntries(StopApp) TCMap<int, int> mp_Items;

	TCFuture<int> f_Read()
	{
		auto Guard = co_await fg_OnResume
			(
				[this]() -> CExceptionPointer
				{
					auto fUnused = [this]
						{
							f_StopApp();
						}
					;
					if (mp_State.m_bStoppingApp)
						return fg_MakeError("Stopped");
					return {};
				}
			)
		;
		auto &Entry = mp_Items[1];
		co_await fg_Round90Suspend();
		co_return Entry;
	}
};

// A pointer member of an entry through a long chain of references to it
// points to what the entry doesn't keep.
TCFuture<int> fg_Round91Work();
struct CRound91Entry
{
	int *m_pValue;
};
struct CRound91Actor : CActor
{
	DMibActorEntries() TCMap<int, CRound91Entry> mp_Items;

	TCFuture<int> f_Read()
	{
		auto &pValue0 = mp_Items[1].m_pValue;
		auto &pValue1 = pValue0;
		auto &pValue2 = pValue1;
		auto &pValue3 = pValue2;
		auto &pValue4 = pValue3;
		int *pValue = pValue4;
		co_await fg_Round91Work();
		co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: 'pValue' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
	}
};

// A helper that only puts back what it is given doesn't look it up again,
// a phase entry given another actor doesn't start the own one's phase, a
// lambda called only on some paths doesn't refresh for sure, and one that
// looks up again what it captured before it reads it reads that.
TCFuture<int> fg_Round92Suspend();
void fg_Round92Restore(int *&o_pValue, int *_pSaved)
{
	o_pValue = _pSaved;
}
struct CRound92Restore : CActor
{
	TCMap<int, int> mp_Items;

	TCFuture<int> f_Read()
	{
		int *pValue = mp_Items.f_FindEqual(0);
		int *pSaved = pValue;
		auto Guard = co_await fg_OnResume
			(
				[&]() -> CExceptionPointer
				{
					fg_Round92Restore(pValue, pSaved);
					return {};
				}
			)
		;
		co_await fg_Round92Suspend();
		co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: 'pValue' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
	}
};
struct CRound92Other : CDistributedAppActor
{
	DMibActorEntries(StopApp) TCMap<int, int> mp_Items;

	DMibActorPhaseEntry(StopApp)
	static TCFuture<int> fs_StopOther(TCActor<CRound92Other> _Other)
	{
		return _Other(&CRound92Other::f_StopApp);
	}

	TCFuture<int> f_Read(TCActor<CRound92Other> _Other)
	{
		auto Guard = co_await fp_CheckStoppedOrDestroyedOnResume();
		auto Resume = co_await fg_OnResume
			(
				[_Other]() -> CExceptionPointer
				{
					fs_StopOther(_Other);
					return {};
				}
			)
		;
		int &Value = mp_Items[0];
		co_await fg_Round92Suspend();
		co_return Value;
	}
};
struct CRound92Value
{
	int m_Value;
};
struct CRound92Lambdas : CActor
{
	TCMap<int, CRound92Value> mp_Items;

	TCFuture<int> f_Maybe(bool _bRefresh)
	{
		CRound92Value *pValue = mp_Items.f_FindEqual(1);
		auto fRefresh = [this, &pValue]
			{
				pValue = mp_Items.f_FindEqual(1);
			}
		;
		auto fMaybeRefresh = [&fRefresh, _bRefresh]
			{
				if (_bRefresh)
					fRefresh();
			}
		;
		co_await fg_Round92Suspend();
		fMaybeRefresh();
		co_return pValue ? pValue->m_Value : 0;
// CHECK-MESSAGES: :[[@LINE-1]]:22: warning: 'pValue' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
	}

	TCFuture<int> f_Again()
	{
		CRound92Value *pValue = mp_Items.f_FindEqual(1);
		auto fRead = [this, pValue]() mutable
			{
				pValue = mp_Items.f_FindEqual(1);
				return pValue ? pValue->m_Value : 0;
			}
		;
		co_await fg_Round92Suspend();
		co_return fRead();
	}
};

TCFuture<int> fg_Round93Suspend();
void fg_Round93Noop();

// A guard chosen through deeply nested conditionals, some alternatives of
// which don't guard.
#define DRound93Step(d_Value) (_bSkip ? co_await f_CheckDestroyedOnResume() : (d_Value))

struct CRound93DeepGuard : public CPhaseActor
{
	DMibActorEntries(Destroy) TCMap<int, int> mp_Items;

	TCFuture<int> f_Work(bool _bSkip)
	{
		auto Guard = DRound93Step(DRound93Step(DRound93Step(DRound93Step(DRound93Step(DRound93Step(DRound93Step(DRound93Step(DRound93Step(
			co_await fg_OnResume([]() -> CExceptionPointer { return {}; })
		)))))))));

		auto &Entry = mp_Items[1];
		co_await fg_Round93Suspend();
		co_return Entry;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'Entry' refers into the data of the actor and is used after a suspension; 'mp_Items' loses entries only in Destroy, so a guard of that phase that is alive at the use, like f_CheckDestroyedOnResume, makes it safe [malterlib-coroutine-actor-reference]
	}
};

// A lambda that stops the app after many statements.
#define DRound93Noop2 fg_Round93Noop(); fg_Round93Noop();
#define DRound93Noop8 DRound93Noop2 DRound93Noop2 DRound93Noop2 DRound93Noop2
#define DRound93Noop64 DRound93Noop8 DRound93Noop8 DRound93Noop8 DRound93Noop8 DRound93Noop8 DRound93Noop8 DRound93Noop8 DRound93Noop8

struct CRound93LongStop : public CDistributedAppActor
{
	DMibActorEntries(StopApp) TCMap<int, int> mp_Items;

	DMibActorPhase(StopApp)
	TCFuture<int> fp_StopApp() override
	{
		mp_Items.f_Clear();
		co_return 0;
	}

	TCFuture<int> f_Work()
	{
		auto Guard = co_await fp_CheckStoppedOrDestroyedOnResume();
		auto &Entry = mp_Items[1];
		co_await fg_Round93Suspend();

		auto fStop = [this] { DRound93Noop64 f_StopApp(); };
		fStop();
		co_return Entry;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'Entry' refers into the data of the actor and is used after a suspension; 'mp_Items' loses entries only in StopApp, so a guard of that phase that is alive at the use, like fp_CheckStoppedOrDestroyedOnResume, makes it safe [malterlib-coroutine-actor-reference]
	}
};

// What a helper returns of its argument, and only the branch a constant
// condition chooses.
int g_Round93Global;
int *fg_Round93Identity(int *_pValue)
{
	return _pValue;
}

struct CRound93Lookups : public CActor
{
	TCMap<int, int> m_Items;

	TCFuture<int> f_Identity()
	{
		int *pValue = fg_Round93Identity(m_Items.f_FindEqual(1));
		co_await fg_Round93Suspend();
		co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: 'pValue' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
	}

	TCFuture<int> f_ConstantCondition()
	{
		int *pValue = false ? m_Items.f_FindEqual(1) : &g_Round93Global;
		co_await fg_Round93Suspend();
		co_return *pValue;
	}
};

TCFuture<int> fg_Round94Suspend();

// A virtual function, called on the object, can run an override that starts
// the phase.
struct CRound94Base : CDistributedAppActor
{
	DMibActorEntries(StopApp) TCMap<int, int> m_Items;
	virtual void f_Tick()
	{
	}

	TCFuture<int> f_Run()
	{
		auto Guard = co_await fp_CheckStoppedOrDestroyedOnResume();
		auto *pEntry = m_Items.f_FindEqual(1);
		f_Tick();
		co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: 'pEntry' refers into the data of the actor and is used after a suspension; 'm_Items' loses entries only in StopApp, so a guard of that phase that is alive at the use, like fp_CheckStoppedOrDestroyedOnResume, makes it safe [malterlib-coroutine-actor-reference]
	}
};

struct CRound94Derived : CRound94Base
{
	DMibActorPhaseEntry(StopApp) void f_Tick() override
	{
		mp_State.m_bStoppingApp = true;
		m_Items.f_Clear();
	}
};

// A pointer to a member function that can't be resolved can be any of them,
// and helpers are followed however many there are.
struct CRound94Unresolved : CDistributedAppActor
{
	DMibActorEntries(StopApp) TCMap<int, int> m_Items;

	DMibActorPhaseEntry(StopApp) void f_Stop()
	{
		mp_State.m_bStoppingApp = true;
		m_Items.f_Clear();
	}

	TCFuture<int> f_Run(void (CRound94Unresolved::*_fChange)())
	{
		auto Guard = co_await fp_CheckStoppedOrDestroyedOnResume();
		auto *pEntry = m_Items.f_FindEqual(1);
		(this->*_fChange)();
		co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: 'pEntry' refers into the data of the actor and is used after a suspension; 'm_Items' loses entries only in StopApp, so a guard of that phase that is alive at the use, like fp_CheckStoppedOrDestroyedOnResume, makes it safe [malterlib-coroutine-actor-reference]
	}

	template <int t_Depth>
	void f_Change()
	{
		if constexpr (t_Depth != 0)
			f_Change<t_Depth - 1>();
		else
			f_Stop();
	}

	TCFuture<int> f_Deep()
	{
		auto Guard = co_await fp_CheckStoppedOrDestroyedOnResume();
		auto *pEntry = m_Items.f_FindEqual(1);
		f_Change<300>();
		co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: 'pEntry' refers into the data of the actor and is used after a suspension; 'm_Items' loses entries only in StopApp, so a guard of that phase that is alive at the use, like fp_CheckStoppedOrDestroyedOnResume, makes it safe [malterlib-coroutine-actor-reference]
	}
};

// A constructor given another object stops that one, not the actor.
struct CRound94LocalState
{
	DMibActorEntries(StopApp) TCMap<int, int> m_Items;
	DMibActorPhaseFlag(StopApp) bool m_bStopping = false;

	DMibActorPhaseEntry(StopApp) void f_Stop()
	{
		m_bStopping = true;
		m_Items.f_Clear();
	}
};

struct CRound94Stopper
{
	CRound94Stopper(CRound94LocalState *_pState)
	{
		_pState->f_Stop();
	}
};

struct CRound94ConstructorApp : CDistributedAppActor
{
	DMibActorEntries(StopApp) TCMap<int, int> m_Items;

	TCFuture<int> f_Run()
	{
		auto Guard = co_await fp_CheckStoppedOrDestroyedOnResume();
		auto *pEntry = m_Items.f_FindEqual(1);
		auto Callback = co_await fg_OnResume
			(
				[]() -> CExceptionPointer
				{
					CRound94LocalState Local;
					CRound94Stopper Stopper(&Local);
					return {};
				}
			)
		;
		co_await fg_Round94Suspend();
		co_return *pEntry;
	}
};

// A helper looks it up again only when it does on every path, not after it
// can return first.
void fg_Round94Refresh(TCMap<int, int> &_Items, int *&o_pValue)
{
	if (o_pValue)
		return;
	o_pValue = _Items.f_FindEqual(1);
}

void fg_Round94Cast(int *&o_pValue)
{
	o_pValue = static_cast<int *>(o_pValue);
}

struct CRound94HelperRefresh : CActor
{
	TCMap<int, int> m_Items;

	TCFuture<int> f_Conditional()
	{
		int *pValue = &m_Items[1];
		auto Guard = co_await fg_OnResume
			(
				[&]() -> CExceptionPointer
				{
					fg_Round94Refresh(m_Items, pValue);
					return {};
				}
			)
		;
		co_await fg_Round94Suspend();
		co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: 'pValue' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
	}

	TCFuture<int> f_Cast()
	{
		int *pValue = &m_Items[1];
		auto Guard = co_await fg_OnResume
			(
				[&]() -> CExceptionPointer
				{
					fg_Round94Cast(pValue);
					return {};
				}
			)
		;
		co_await fg_Round94Suspend();
		co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: 'pValue' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
	}
};

// The storage of a `std::array` lives with the actor, also through its
// accessors.
namespace std
{
	template <typename t_CType, unsigned long t_Size>
	struct array
	{
		t_CType *data();
		t_CType &front();
		t_CType &operator [] (unsigned long _Index);

		t_CType m_Elements[t_Size];
	};
}

struct CRound94Array : CActor
{
	std::array<int, 2> m_Values;

	TCFuture<int> f_Data()
	{
		int *pValues = m_Values.data();
		co_await fg_Round94Suspend();
		co_return pValues[0];
	}

	TCFuture<int> f_Front()
	{
		int &Value = m_Values.front();
		co_await fg_Round94Suspend();
		co_return Value;
	}
};

TCFuture<int> fg_Round95Suspend();

// What a callback calls through an unchanged pointer to the object, or an
// override of what it calls, can remove what it looked up again.
struct CRound95Alias : CPhaseActor
{
	TCMap<int, int> m_Items;
	void f_Tidy()
	{
		m_Items.f_Clear();
	}

	TCFuture<int> f_Run()
	{
		int *pValue = &m_Items[0];
		auto *pThis = this;
		auto Guard = co_await fg_OnResume
			(
				[pThis, &pValue]() -> CExceptionPointer
				{
					pValue = &pThis->m_Items[0];
					pThis->f_Tidy();
					return {};
				}
			)
		;
		co_await fg_Round95Suspend();
		co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: 'pValue' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
	}
};

struct CRound95Virtual : CPhaseActor
{
	TCMap<int, int> m_Items;
	virtual void f_Tidy()
	{
	}

	TCFuture<int> f_Run()
	{
		int *pValue = &m_Items[0];
		auto Guard = co_await fg_OnResume
			(
				[this, &pValue]() -> CExceptionPointer
				{
					pValue = &m_Items[0];
					f_Tidy();
					return {};
				}
			)
		;
		co_await fg_Round95Suspend();
		co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: 'pValue' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
	}
};

struct CRound95Derived : CRound95Virtual
{
	void f_Tidy() override
	{
		m_Items.f_Clear();
	}
};

// A static local doesn't keep the entry it points to.
struct CRound95Static : CPhaseActor
{
	TCMap<int, int> m_Items;

	TCFuture<int> f_Run()
	{
		static int *s_pValue = &m_Items[0];
		co_await fg_Round95Suspend();
		co_return *s_pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: 's_pValue' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
	}
};

TCFuture<int> fg_Round96Suspend();

// A no-op called through a local pointer to it doesn't look it up again,
// and `std::ranges::swap` swaps pointers like `std::swap`.
void fg_Round96NoOp(int *&o_pValue)
{
}

namespace std::ranges
{
	inline constexpr struct CSwap
	{
		template <typename tf_CType>
		void operator ()(tf_CType &o_Left, tf_CType &o_Right) const;
	} swap{};
}

struct CRound96NoOp : CDistributedAppActor
{
	TCMap<int, int> m_Items;

	TCFuture<int> f_Indirect()
	{
		int *pValue = &m_Items[1];
		auto Guard = co_await fg_OnResume
			(
				[&pValue]() -> CExceptionPointer
				{
					auto fNoOp = &fg_Round96NoOp;
					fNoOp(pValue);
					return {};
				}
			)
		;
		co_await fg_Round96Suspend();
		co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: 'pValue' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
	}

	TCFuture<int> f_RangesSwap()
	{
		int Local = 0;
		int *pValue = &Local;
		int *pOther = &m_Items[1];
		std::ranges::swap(pValue, pOther);
		co_await fg_Round96Suspend();
		co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: 'pValue' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
	}

	TCFuture<int> f_Plus()
	{
		int *pValue = +m_Items.f_FindEqual(1);
		co_await fg_Round96Suspend();
		co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: 'pValue' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
	}
};

// The iterator of a vector, which libc++ names `__wrap_iter`.
namespace std
{
	template <typename t_CIterator>
	struct __wrap_iter
	{
		int &operator *() const;
	};

	template <typename t_CType>
	struct vector
	{
		using iterator = __wrap_iter<t_CType *>;
		iterator begin();
	};
}

struct CRound96Vector : CActor
{
	std::vector<int> m_Values;

	TCFuture<int> f_Run()
	{
		auto iValue = m_Values.begin();
		co_await fg_Round96Suspend();
		co_return *iValue;
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: 'iValue' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
	}
};

// A callback of fg_OnResume that looks the data up again, but then removes
// entries, doesn't re-validate it.
struct CRound98Refresh : CActor
{
	TCMap<int, int> mp_Items;

	TCFuture<int> f_Run()
	{
		int *pValue = mp_Items.f_FindEqual(1);
		auto Resume = co_await fg_OnResume
			(
				[&]() -> CExceptionPointer
				{
					pValue = mp_Items.f_FindEqual(1);
					mp_Items = {};
					return {};
				}
			)
		;
		co_await fg_Suspend();
		co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: 'pValue' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
	}
};

// Setting a result from another result, which can be unset, doesn't set it:
// the callback can let the coroutine continue in StopApp.
struct CRound98SetFrom : CDistributedAppActor
{
	DMibActorEntries(StopApp) TCMap<int, int> mp_Items;

	TCFuture<int> f_Run()
	{
		auto Resume = co_await fg_OnResume
			(
				[this]() -> TCAsyncResult<int>
				{
					if (mp_State.m_bStoppingApp)
					{
						TCAsyncResult<int> Empty;
						TCAsyncResult<int> Result;
						Result.f_SetResult(Empty);
						return Result;
					}
					return {};
				}
			)
		;
		int *pValue = mp_Items.f_FindEqual(1);
		co_await fg_Suspend();
		co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: 'pValue' refers into the data of the actor and is used after a suspension; 'mp_Items' loses entries only in StopApp, so a guard of that phase that is alive at the use, like fp_CheckStoppedOrDestroyedOnResume, makes it safe [malterlib-coroutine-actor-reference]
	}
};

// Through any number of local references to the data.
struct CRound98Aliases : CActor
{
	TCMap<int, int> mp_Items;

	TCFuture<int> f_Run()
	{
		auto &First = mp_Items;
		auto &Second = First;
		auto &Third = Second;
		auto &Fourth = Third;
		auto &Fifth = Fourth;
		int *pValue = Fifth.f_FindEqual(1);
		co_await fg_Suspend();
		co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: 'pValue' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
	}
};

// `begin` of a fixed array of the actor, which lives with it.
namespace NRound98
{
	template <typename tf_CType, decltype(sizeof(0)) tf_Size>
	tf_CType *begin(tf_CType (&_Array)[tf_Size])
	{
		return _Array;
	}
}

struct CRound98Array : CActor
{
	int m_Values[2] = {};

	TCFuture<int> f_Run()
	{
		int *pValue = NRound98::begin(m_Values);
		co_await fg_Suspend();
		co_return *pValue;
	}
};

// A helper given the own object and another actor starts the phase of the
// own one too.
struct CRound99Both : CDistributedAppActor
{
	bool m_bStopNow = false;
	DMibActorEntries(StopApp) TCMap<int, int> mp_Items;

	DMibActorPhase(StopApp) TCFuture<int> fp_StopApp() override
	{
		mp_Items.f_Clear();
		co_return 0;
	}

	static void fs_Stop(CRound99Both *_pThis, CRound99Both *_pOther)
	{
		_pThis->f_StopApp();
	}

	TCFuture<int> f_Run(CRound99Both *_pOther)
	{
		auto Guard = co_await fp_CheckStoppedOrDestroyedOnResume();
		auto Callback = co_await fg_OnResume
			(
				[this, _pOther]() -> CExceptionPointer
				{
					if (m_bStopNow)
						fs_Stop(this, _pOther);
					return {};
				}
			)
		;
		int &Value = mp_Items[1];
		co_await fg_Suspend();
		co_return Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'Value' refers into the data of the actor and is used after a suspension; 'mp_Items' loses entries only in StopApp, so a guard of that phase that is alive at the use, like fp_CheckStoppedOrDestroyedOnResume, makes it safe [malterlib-coroutine-actor-reference]
	}
};

// A helper that starts the phase in a lambda it calls.
struct CRound99Wrapped : CDistributedAppActor
{
	DMibActorEntries(StopApp) TCMap<int, int> mp_Items;

	DMibActorPhase(StopApp) TCFuture<int> fp_StopApp() override
	{
		mp_Items.f_Clear();
		co_return 0;
	}

	void f_StopWrapper()
	{
		[this]
		{
			f_StopApp();
		}();
	}

	TCFuture<int> f_Run()
	{
		auto Guard = co_await fp_CheckStoppedOrDestroyedOnResume();
		int &Value = mp_Items[1];
		co_await fg_Suspend();
		f_StopWrapper();
		co_return Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'Value' refers into the data of the actor and is used after a suspension; 'mp_Items' loses entries only in StopApp, so a guard of that phase that is alive at the use, like fp_CheckStoppedOrDestroyedOnResume, makes it safe [malterlib-coroutine-actor-reference]
	}
};

// A helper that gives back what it was given, offset, doesn't look it up
// again.
void fg_Round99Reset(int *&o_pValue)
{
	o_pValue = o_pValue + 0;
}

struct CRound99Offset : CActor
{
	TCMap<int, int> mp_Items;

	TCFuture<int> f_Run()
	{
		int *pValue = mp_Items.f_FindEqual(0);
		auto Guard = co_await fg_OnResume
			(
				[&]() -> CExceptionPointer
				{
					fg_Round99Reset(pValue);
					return {};
				}
			)
		;
		co_await fg_Suspend();
		co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: 'pValue' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
	}
};

// A virtual function that makes the error can be overridden by one that
// gives success.
struct CRound100Virtual : CPhaseActor
{
	DMibActorEntries(Destroy) TCMap<int, int> mp_Items;

	TCFuture<int> fp_Destroy() override
	{
		mp_Items.f_Clear();
		co_return 0;
	}

	virtual CExceptionPointer f_Result()
	{
		return fg_MakeError("Destroyed");
	}

	TCFuture<int> f_Run()
	{
		auto Guard = co_await fg_OnResume
			(
				[this]() -> CExceptionPointer
				{
					if (f_IsDestroyed())
						return f_Result();
					return {};
				}
			)
		;
		int &Value = mp_Items[1];
		co_await fg_Suspend();
		co_return Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'Value' refers into the data of the actor and is used after a suspension; 'mp_Items' loses entries only in Destroy, so a guard of that phase that is alive at the use, like f_CheckDestroyedOnResume, makes it safe [malterlib-coroutine-actor-reference]
	}
};

// A helper that looks up again, and then gives back what the caller had.
void fg_Round100Refresh(TCMap<int, int> &o_Items, int *&o_pEntry, int *_pSaved)
{
	o_pEntry = o_Items.f_FindEqual(1);
	o_pEntry = _pSaved;
}

struct CRound100Restored : CActor
{
	TCMap<int, int> mp_Items;

	TCFuture<int> f_Run()
	{
		int *pEntry = mp_Items.f_FindEqual(1);
		int *pSaved = pEntry;
		auto Guard = co_await fg_OnResume
			(
				[this, &pEntry, pSaved]() -> CExceptionPointer
				{
					fg_Round100Refresh(mp_Items, pEntry, pSaved);
					return {};
				}
			)
		;
		co_await fg_Suspend();
		co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
	}
};

// The data of what isn't an actor, given an actor.
struct CRound100Worker : CAllowUnsafeThis
{
	TCMap<int, int> mp_Items;

	TCFuture<int> f_Run(CActor *_pActor)
	{
		int &Value = mp_Items[1];
		co_await fg_Suspend();
		co_return Value;
	}
};

// An unchanged alias of a parameter given another object is of that object.
struct CRound100ForeignState
{
	DMibActorPhaseFlag(StopApp) bool m_bStopping = false;
	DMibActorPhaseEntry(StopApp) void f_Stop()
	{
		m_bStopping = true;
	}
};

struct CRound100Alias : CDistributedAppActor
{
	DMibActorEntries(StopApp) TCMap<int, int> mp_Items;

	static void fs_Tick(CRound100ForeignState *_pThis, CRound100Alias *_pKeepThis)
	{
		auto *pAlias = _pThis;
		if (!_pKeepThis->f_IsDestroyed())
			pAlias->f_Stop();
	}

	TCFuture<int> f_Run()
	{
		CRound100ForeignState Other;
		auto Guard = co_await fp_CheckStoppedOrDestroyedOnResume();
		auto Tick = co_await fg_OnResume
			(
				[this, &Other]() -> CExceptionPointer
				{
					fs_Tick(&Other, this);
					return {};
				}
			)
		;
		int &Value = mp_Items[1];
		co_await fg_Suspend();
		co_return Value;
	}
};

// Through a local reference made of a conditional or a comma.
struct CRound100Conditional : CActor
{
	TCMap<int, int> mp_Items;

	TCFuture<int> f_Run(bool _bChoose)
	{
		auto &Alias = _bChoose ? mp_Items : mp_Items;
		int *pValue = Alias.f_FindEqual(0);
		auto &Last = (0, mp_Items);
		int *pLast = Last.f_FindEqual(0);
		co_await fg_Suspend();
		co_return *pValue + *pLast;
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: 'pValue' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
// CHECK-MESSAGES: :[[@LINE-2]]:24: warning: 'pLast' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
	}
};

// A chain of helpers given the same reference many times is told once each.
struct CRound100Map
{
	int *f_FindEqual(int _Key);
};
CRound100Map &fg_Round100Pick(CRound100Map &, CRound100Map &, CRound100Map &, CRound100Map &, CRound100Map &, CRound100Map &, CRound100Map &, CRound100Map &, CRound100Map &, CRound100Map &, CRound100Map &, CRound100Map &, CRound100Map &, CRound100Map &, CRound100Map &, CRound100Map &, CRound100Map &, CRound100Map &, CRound100Map &, CRound100Map &, CRound100Map &, CRound100Map &, CRound100Map &, CRound100Map &, CRound100Map &, CRound100Map &, CRound100Map &, CRound100Map &, CRound100Map &, CRound100Map &, CRound100Map &, CRound100Map &, CRound100Map &, CRound100Map &, CRound100Map &, CRound100Map &, CRound100Map &, CRound100Map &, CRound100Map &, CRound100Map &, CRound100Map &, CRound100Map &, CRound100Map &, CRound100Map &, CRound100Map &, CRound100Map &, CRound100Map &, CRound100Map &, CRound100Map &, CRound100Map &, CRound100Map &, CRound100Map &, CRound100Map &, CRound100Map &, CRound100Map &, CRound100Map &, CRound100Map &, CRound100Map &, CRound100Map &, CRound100Map &, CRound100Map &, CRound100Map &, CRound100Map &, CRound100Map &, CRound100Map &, CRound100Map &, CRound100Map &, CRound100Map &, CRound100Map &, CRound100Map &, CRound100Map &, CRound100Map &, CRound100Map &, CRound100Map &, CRound100Map &, CRound100Map &, CRound100Map &, CRound100Map &, CRound100Map &, CRound100Map &, CRound100Map &, CRound100Map &, CRound100Map &, CRound100Map &, CRound100Map &, CRound100Map &, CRound100Map &, CRound100Map &, CRound100Map &, CRound100Map &, CRound100Map &, CRound100Map &, CRound100Map &, CRound100Map &, CRound100Map &, CRound100Map &, CRound100Map &, CRound100Map &, CRound100Map &, CRound100Map &);

struct CRound100Chain : CActor
{
	TCFuture<int> f_Run()
	{
		CRound100Map Local;
		CRound100Map &Ref0 = Local;
		CRound100Map &Ref1 = fg_Round100Pick(Ref0, Ref0, Ref0, Ref0, Ref0, Ref0, Ref0, Ref0, Ref0, Ref0, Ref0, Ref0, Ref0, Ref0, Ref0, Ref0, Ref0, Ref0, Ref0, Ref0, Ref0, Ref0, Ref0, Ref0, Ref0, Ref0, Ref0, Ref0, Ref0, Ref0, Ref0, Ref0, Ref0, Ref0, Ref0, Ref0, Ref0, Ref0, Ref0, Ref0, Ref0, Ref0, Ref0, Ref0, Ref0, Ref0, Ref0, Ref0, Ref0, Ref0, Ref0, Ref0, Ref0, Ref0, Ref0, Ref0, Ref0, Ref0, Ref0, Ref0, Ref0, Ref0, Ref0, Ref0, Ref0, Ref0, Ref0, Ref0, Ref0, Ref0, Ref0, Ref0, Ref0, Ref0, Ref0, Ref0, Ref0, Ref0, Ref0, Ref0, Ref0, Ref0, Ref0, Ref0, Ref0, Ref0, Ref0, Ref0, Ref0, Ref0, Ref0, Ref0, Ref0, Ref0, Ref0, Ref0, Ref0, Ref0, Ref0, Ref0);
		CRound100Map &Ref2 = fg_Round100Pick(Ref1, Ref1, Ref1, Ref1, Ref1, Ref1, Ref1, Ref1, Ref1, Ref1, Ref1, Ref1, Ref1, Ref1, Ref1, Ref1, Ref1, Ref1, Ref1, Ref1, Ref1, Ref1, Ref1, Ref1, Ref1, Ref1, Ref1, Ref1, Ref1, Ref1, Ref1, Ref1, Ref1, Ref1, Ref1, Ref1, Ref1, Ref1, Ref1, Ref1, Ref1, Ref1, Ref1, Ref1, Ref1, Ref1, Ref1, Ref1, Ref1, Ref1, Ref1, Ref1, Ref1, Ref1, Ref1, Ref1, Ref1, Ref1, Ref1, Ref1, Ref1, Ref1, Ref1, Ref1, Ref1, Ref1, Ref1, Ref1, Ref1, Ref1, Ref1, Ref1, Ref1, Ref1, Ref1, Ref1, Ref1, Ref1, Ref1, Ref1, Ref1, Ref1, Ref1, Ref1, Ref1, Ref1, Ref1, Ref1, Ref1, Ref1, Ref1, Ref1, Ref1, Ref1, Ref1, Ref1, Ref1, Ref1, Ref1, Ref1);
		CRound100Map &Ref3 = fg_Round100Pick(Ref2, Ref2, Ref2, Ref2, Ref2, Ref2, Ref2, Ref2, Ref2, Ref2, Ref2, Ref2, Ref2, Ref2, Ref2, Ref2, Ref2, Ref2, Ref2, Ref2, Ref2, Ref2, Ref2, Ref2, Ref2, Ref2, Ref2, Ref2, Ref2, Ref2, Ref2, Ref2, Ref2, Ref2, Ref2, Ref2, Ref2, Ref2, Ref2, Ref2, Ref2, Ref2, Ref2, Ref2, Ref2, Ref2, Ref2, Ref2, Ref2, Ref2, Ref2, Ref2, Ref2, Ref2, Ref2, Ref2, Ref2, Ref2, Ref2, Ref2, Ref2, Ref2, Ref2, Ref2, Ref2, Ref2, Ref2, Ref2, Ref2, Ref2, Ref2, Ref2, Ref2, Ref2, Ref2, Ref2, Ref2, Ref2, Ref2, Ref2, Ref2, Ref2, Ref2, Ref2, Ref2, Ref2, Ref2, Ref2, Ref2, Ref2, Ref2, Ref2, Ref2, Ref2, Ref2, Ref2, Ref2, Ref2, Ref2, Ref2);
		CRound100Map &Ref4 = fg_Round100Pick(Ref3, Ref3, Ref3, Ref3, Ref3, Ref3, Ref3, Ref3, Ref3, Ref3, Ref3, Ref3, Ref3, Ref3, Ref3, Ref3, Ref3, Ref3, Ref3, Ref3, Ref3, Ref3, Ref3, Ref3, Ref3, Ref3, Ref3, Ref3, Ref3, Ref3, Ref3, Ref3, Ref3, Ref3, Ref3, Ref3, Ref3, Ref3, Ref3, Ref3, Ref3, Ref3, Ref3, Ref3, Ref3, Ref3, Ref3, Ref3, Ref3, Ref3, Ref3, Ref3, Ref3, Ref3, Ref3, Ref3, Ref3, Ref3, Ref3, Ref3, Ref3, Ref3, Ref3, Ref3, Ref3, Ref3, Ref3, Ref3, Ref3, Ref3, Ref3, Ref3, Ref3, Ref3, Ref3, Ref3, Ref3, Ref3, Ref3, Ref3, Ref3, Ref3, Ref3, Ref3, Ref3, Ref3, Ref3, Ref3, Ref3, Ref3, Ref3, Ref3, Ref3, Ref3, Ref3, Ref3, Ref3, Ref3, Ref3, Ref3);
		int *pValue = Ref4.f_FindEqual(0);
		co_await fg_Suspend();
		co_return *pValue;
	}
};

// What a helper whose body can be seen returns.
TCMap<int, int> &fg_Round100Second(TCMap<int, int> &_First, TCMap<int, int> &_Second)
{
	return _Second;
}

struct CRound100Returned : CActor
{
	TCMap<int, int> mp_Items;

	TCFuture<int> f_Run()
	{
		TCMap<int, int> Local;
		auto &Alias = fg_Round100Second(mp_Items, Local);
		int *pValue = Alias.f_FindEqual(0);
		co_await fg_Suspend();
		co_return *pValue;
	}
};

// A virtual function of a temporary, whose type is known, can't run an
// override.
struct CRound101Error
{
	CRound101Error(char const *_pMessage);
	virtual ~CRound101Error();
	virtual CExceptionPointer f_ExceptionPointer() const
	{
		return CException("Error", "Deleted");
	}
};

struct CRound101Temporary : CActor
{
	TCMap<int, int> mp_Items;

	TCFuture<int> f_Run()
	{
		int &Value = mp_Items[1];
		auto Guard = co_await fg_OnResume
			(
				[&]() -> CExceptionPointer
				{
					if (mp_Items.f_FindEqual(1) != &Value)
						return CRound101Error("Deleted").f_ExceptionPointer();
					return {};
				}
			)
		;
		co_await fg_Suspend();
		co_return Value;
	}
};

// A lambda held in a local, dispatched after a suspension.
struct CRound101Stored : CPhaseActor
{
	TCMap<int, int> mp_Items;

	TCFuture<int> f_Run()
	{
		int *pValue = mp_Items.f_FindEqual(1);
		auto fRead = [pValue]
// CHECK-MESSAGES: :[[@LINE-1]]:17: warning: 'pValue' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
			{
				return *pValue;
			}
		;
		co_await fg_Suspend();
		co_return co_await (g_Dispatch / fRead);
	}
};

// A helper that gives back a local copy of what it was given.
void fg_Round101Unchanged(int *&o_pEntry)
{
	int *pSaved = o_pEntry;
	o_pEntry = pSaved;
}

struct CRound101Copy : CActor
{
	TCMap<int, int> mp_Items;

	TCFuture<int> f_Run()
	{
		int *pValue = mp_Items.f_FindEqual(1);
		auto Resume = co_await fg_OnResume
			(
				[&]() -> CExceptionPointer
				{
					fg_Round101Unchanged(pValue);
					return {};
				}
			)
		;
		co_await fg_Suspend();
		co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: 'pValue' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
	}
};

// A lambda made inside a called one, but not called, doesn't use what it
// captures.
struct CRound101Unused : CActor
{
	TCMap<int, int> mp_Items;

	TCFuture<int> f_Run()
	{
		int *pValue = mp_Items.f_FindEqual(1);
		co_await fg_Suspend();
		auto fOuter = [&]
			{
				auto fUnused = [&]
					{
						return *pValue;
					}
				;
			}
		;
		fOuter();
		co_return 0;
	}
};

// A helper given the collection that removes entries of it, after the lookup
// that re-validates.
void fg_Round102Erase(TCMap<int, int> &o_Items)
{
	o_Items.f_Clear();
}

struct CRound102Erase : CActor
{
	TCMap<int, int> mp_Items;

	TCFuture<int> f_Run()
	{
		int *pValue = mp_Items.f_FindEqual(0);
		auto Guard = co_await fg_OnResume
			(
				[this, &pValue]() -> CExceptionPointer
				{
					pValue = mp_Items.f_FindEqual(0);
					fg_Round102Erase(mp_Items);
					if (!pValue)
						return fg_MakeError("Gone");
					return {};
				}
			)
		;
		co_await fg_Suspend();
		co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: 'pValue' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
	}
};

// A helper that puts back a pointer saved in a global.
int *g_pRound102Saved;
void fg_Round102PutBack(int *&o_pValue)
{
	o_pValue = g_pRound102Saved;
}

struct CRound102Saved : CActor
{
	TCMap<int, int> mp_Items;

	TCFuture<int> f_Run()
	{
		int *pValue = mp_Items.f_FindEqual(0);
		g_pRound102Saved = pValue;
		auto Guard = co_await fg_OnResume
			(
				[&pValue]() -> CExceptionPointer
				{
					fg_Round102PutBack(pValue);
					return {};
				}
			)
		;
		co_await fg_Suspend();
		co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: 'pValue' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
	}
};

// What isn't evaluated doesn't remove entries.
struct CRound102Unevaluated : CActor
{
	TCMap<int, int> mp_Items;

	TCFuture<int> f_Run()
	{
		int *pValue = mp_Items.f_FindEqual(0);
		auto Guard = co_await fg_OnResume
			(
				[this, &pValue]() -> CExceptionPointer
				{
					pValue = mp_Items.f_FindEqual(0);
					(void)noexcept(mp_Items.f_Clear());
					if (!pValue)
						return fg_MakeError("Gone");
					return {};
				}
			)
		;
		co_await fg_Suspend();
		co_return *pValue;
	}
};

// A guard with an init statement.
struct CRound102Init : CDistributedAppActor
{
	DMibActorEntries(Destroy, StopApp) TCMap<int, int> mp_Items;

	TCFuture<int> f_Run()
	{
		auto Guard = co_await fg_OnResume
			(
				[this]() -> CExceptionPointer
				{
					if (int Unused = 0; mp_State.m_bStoppingApp || f_IsDestroyed())
						return fg_MakeError("Gone");
					return {};
				}
			)
		;
		auto *pValue = mp_Items.f_FindEqual(0);
		co_await fg_Suspend();
		co_return *pValue;
	}
};

// Either of a conditional pointer, accessed after a suspension.
struct CRound102Conditional : CActor
{
	TCMap<int, int> mp_Items;

	TCFuture<int> f_Run(bool _bChoose)
	{
		int *pValue = mp_Items.f_FindEqual(1);
		co_return (_bChoose ? pValue : pValue)[(co_await fg_Suspend(), 0)];
// CHECK-MESSAGES: :[[@LINE-1]]:25: warning: 'pValue' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
	}
};

// A refresh before the lambda it calls reads.
struct CRound102Order : CActor
{
	TCMap<int, int> mp_Items;

	TCFuture<int> f_Run()
	{
		auto *pValue = mp_Items.f_FindEqual(1);
		auto fRead = [&]
			{
				return pValue ? *pValue : 0;
			}
		;
		auto fRefreshAndRead = [&]
			{
				pValue = mp_Items.f_FindEqual(1);
				return fRead();
			}
		;
		co_await fg_Suspend();
		co_return fRefreshAndRead();
	}
};

// A guard registered after a callback doesn't protect what the callback reads.
void fg_Round104Read(int _Value);

struct CRound104Order : CPhaseActor
{
	DMibActorEntries(Destroy) TCMap<int, int> mp_Items;

	TCFuture<int> f_Run()
	{
		int &Entry = mp_Items[1];
		auto First = co_await fg_OnResume
			(
				[&Entry]() -> CExceptionPointer
				{
					fg_Round104Read(Entry);
// CHECK-MESSAGES: :[[@LINE-1]]:22: warning: 'Entry' refers into the data of the actor and is used after a suspension; 'mp_Items' loses entries only in Destroy, so a guard of that phase that is alive at the use, like f_CheckDestroyedOnResume, makes it safe [malterlib-coroutine-actor-reference]
					return {};
				}
			)
		;
		auto Guard = co_await f_CheckDestroyedOnResume();
		co_await fg_Suspend();
		co_return 0;
	}

	TCFuture<int> fp_Destroy() override
	{
		mp_Items.f_Clear();
		co_return 0;
	}
};

// A result set from another result, which can be unset, after it was set.
struct CRound104Reset : CPhaseActor
{
	TCMap<int, int> mp_Items;

	TCFuture<int> f_Run()
	{
		int *pValue = &mp_Items[1];
		auto Guard = co_await fg_OnResume
			(
				[this, &pValue]() -> TCAsyncResult<int>
				{
					TCAsyncResult<int> Result;
					if (pValue != mp_Items.f_FindEqual(1))
					{
						Result.f_SetResult(1);
						TCAsyncResult<int> Empty;
						Result.f_SetResult(Empty);
						return Result;
					}
					return {};
				}
			)
		;
		co_await fg_Suspend();
		co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: 'pValue' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
	}
};

// A result set in each branch.
struct CRound104Branches : CPhaseActor
{
	TCMap<int, int> mp_Items;
	bool m_bOther = false;

	TCFuture<int> f_Run()
	{
		int *pValue = &mp_Items[1];
		auto Guard = co_await fg_OnResume
			(
				[this, &pValue]() -> TCAsyncResult<int>
				{
					TCAsyncResult<int> Result;
					if (pValue != mp_Items.f_FindEqual(1))
					{
						if (m_bOther)
							Result.f_SetResult(1);
						else
							Result.f_SetResult(2);
						return Result;
					}
					return {};
				}
			)
		;
		co_await fg_Suspend();
		co_return *pValue;
	}
};

// A static local is made once, by the first call.
struct CRound104Static : CActor
{
	TCMap<int, int> mp_Items;

	TCFuture<int> f_Run()
	{
		co_await fg_Suspend();
		static int *s_pValue = &mp_Items[0];
		co_return *s_pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: 's_pValue' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
	}
};

// An entry accessed after a suspension in the index of an array member.
TCFuture<int> fg_Round105Suspend();

struct CRound105ArrayEntry
{
	int m_Values[2];
};

struct CRound105ArrayIndex : CActor
{
	TCMap<int, CRound105ArrayEntry> mp_Items;

	TCFuture<int> f_Read()
	{
		auto *pEntry = mp_Items.f_FindEqual(1);
		co_return pEntry->m_Values[(co_await fg_Round105Suspend(), 0)];
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
	}
};

// A pointer pointed elsewhere by an awaited coroutine lambda, before it first
// suspends.
struct CRound106Redirected : CActor
{
	TCMap<int, int> mp_Items;

	TCFuture<int> f_Read()
	{
		int Local = 0;
		int *pValue = mp_Items.f_FindEqual(1);
		co_await fg_CallSafe
			(
				[&pValue, &Local]() -> TCFuture<int>
				{
					pValue = &Local;
					co_return 0;
				}
			)
		;
		co_await fg_Round105Suspend();
		co_return *pValue;
	}
};

// A lookup again before a lambda reads it, and a reset after.
struct CRound107Refreshed : CActor
{
	TCMap<int, int> mp_Items;

	TCFuture<int> f_Read()
	{
		auto *pValue = &mp_Items[1];
		auto fRead = [&]
			{
				return *pValue;
			}
		;
		auto fWrapper = [&]
			{
				pValue = &mp_Items[1];
				int Value = fRead();
				pValue = nullptr;
				return Value;
			}
		;
		co_await fg_Suspend();
		co_return fWrapper();
	}
};

// A pointer moved by an offset that suspends, given to a function.
int fg_Round107Read(int const *_pValue);

struct CRound107Offset : CActor
{
	TCMap<int, int> mp_Items;

	TCFuture<int> f_Read()
	{
		int *pValue = mp_Items.f_FindEqual(0);
		co_return fg_Round107Read(pValue + (co_await fg_Suspend(), 0));
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'pValue' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
	}
};

// Not a read in a branch that a constant condition never takes.
struct CRound107NeverRead : CActor
{
	TCMap<int, int> mp_Items;

	TCFuture<int> f_Read()
	{
		int *pValue = mp_Items.f_FindEqual(1);
		co_await fg_Suspend();
		auto fRead = [pValue]
			{
				if (false)
					return *pValue;
				return 0;
			}
		;
		co_return fRead();
	}
};

// A helper lambda that looks it up again before a lambda reads it.
struct CRound109Refreshed : CActor
{
	TCMap<int, int> mp_Items;

	TCFuture<int> f_Read()
	{
		int *pValue = mp_Items.f_FindEqual(1);
		auto fRefresh = [this, &pValue]
			{
				pValue = mp_Items.f_FindEqual(1);
			}
		;
		auto fUse = [&pValue, &fRefresh]
			{
				fRefresh();
				return pValue ? *pValue : 0;
			}
		;
		co_await fg_Suspend();
		co_return fUse();
	}
};

// What a helper returns through conditionals in conditionals.
int *fg_Round109Choose(int *_pValue, bool _bA, bool _bB)
{
	return _bA ? (_bB ? _pValue : nullptr) : nullptr;
}

struct CRound109Choose : CActor
{
	TCMap<int, int> mp_Items;

	TCFuture<int> f_Read(bool _bA, bool _bB)
	{
		int *pValue = fg_Round109Choose(&mp_Items[0], _bA, _bB);
		co_await fg_Suspend();
		co_return pValue ? *pValue : 0;
// CHECK-MESSAGES: :[[@LINE-1]]:23: warning: 'pValue' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
	}
};

// A callback of fg_OnResume that looks an entry up again and then removes it
// through a local iterator can't re-validate it.
struct CRound110RemoveInOnResume : CActor
{
	TCMap<int, int> mp_Items;

	TCFuture<int> f_Read()
	{
		int *pEntry = &mp_Items[1];
		auto OnResume = co_await fg_OnResume
			(
				[&]() -> CExceptionPointer
				{
					pEntry = &mp_Items[1];
					auto iEntry = mp_Items.f_GetIterator(1);
					iEntry.f_Remove();
					return {};
				}
			)
		;
		co_await fg_Suspend();
		co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
	}
};

// A removal flag tested through an atomic load.
struct CRound110Atomic
{
	bool f_Load() const;
	void f_Store(bool _bValue);
};

struct CRound110AtomicEntry
{
	TCSharedPointer<CRound110Atomic> m_pDeleted;
	int m_Value;
	~CRound110AtomicEntry()
	{
		m_pDeleted->f_Store(true);
	}
};

struct CRound110AtomicFlag : CActor
{
	TCMap<int, CRound110AtomicEntry> mp_Items;

	TCFuture<int> f_Read()
	{
		auto *pEntry = mp_Items.f_FindEqual(1);
		auto pDeleted = pEntry->m_pDeleted;
		co_await fg_Suspend();
		if (pDeleted->f_Load())
			co_return 0;
		co_return pEntry->m_Value;
	}
};

// The destructor of a temporary that starts a phase, at the end of the full
// expression.
struct CRound111App : CDistributedAppActor
{
	DMibActorEntries(StopApp) TCMap<int, int> mp_Items;

	DMibActorPhaseEntry(StopApp) void f_StopNow()
	{
		mp_State.m_bStoppingApp = true;
		mp_Items.f_Clear();
	}

	struct CStopper
	{
		CRound111App *m_pThis;
		~CStopper()
		{
			m_pThis->f_StopNow();
		}
	};

	TCFuture<int> f_Run()
	{
		auto Guard = co_await fp_CheckStoppedOrDestroyedOnResume();
		int &Value = mp_Items[1];
		(void)CStopper{this};
		co_return Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'Value' refers into the data of the actor and is used after a suspension; 'mp_Items' loses entries only in StopApp, so a guard of that phase that is alive at the use, like fp_CheckStoppedOrDestroyedOnResume, makes it safe [malterlib-coroutine-actor-reference]
	}
};

// Braced arguments are evaluated in order, so one after the suspension is
// looked up after it.
struct CRound111Copy
{
	CRound111Copy(int _Ignored, int const &_Value)
		: m_Value(_Value)
	{
	}
	int m_Value;
};
struct CRound111CopyFirst
{
	CRound111CopyFirst(int const &_Value, int _Ignored)
		: m_Value(_Value)
	{
	}
	int m_Value;
};
TCFuture<int> fg_Round111Suspend();
struct CRound111Braced : CPhaseActor
{
	TCMap<int, int> mp_Items;

	TCFuture<int> f_After()
	{
		CRound111Copy Copy{co_await fg_Round111Suspend(), mp_Items[1]};
		co_return Copy.m_Value;
	}

	TCFuture<int> f_Before()
	{
		CRound111CopyFirst Copy{mp_Items[1], co_await fg_Round111Suspend()};
// CHECK-MESSAGES: :[[@LINE-1]]:22: warning: the data of the actor that this call uses is looked up before its arguments suspend, during which other calls of the actor can change or erase the data; look it up after the suspension [malterlib-coroutine-actor-reference]
		co_return Copy.m_Value;
	}
};

// A pointer that is assigned a lookup before a lambda captures it.
struct CRound112AssignedActor : CActor
{
	TCMap<int, int> mp_Items;
};
TCFuture<int> fg_Round112Assigned(CRound112AssignedActor *_pActor)
{
	int *p = nullptr;
	p = &_pActor->mp_Items[1];
	return fg_CallSafe
		(
			[p]() -> TCFuture<int>
			{
				co_await fg_Suspend();
				co_return *p;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'p' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
			}
		)
	;
}

// Lambdas called many times are each looked into once.
#define DRound112Step(d_Name, d_Previous) \
	auto d_Name = [&p, &d_Previous] \
		{ \
			d_Previous(); d_Previous(); d_Previous(); d_Previous(); return *p; \
		};
// CHECK-MESSAGES: :[[@LINE-4]]:18: warning: 'p' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
struct CRound112Repeated : CActor
{
	TCMap<int, int> mp_Items;

	TCFuture<int> f_Run()
	{
		int *p = &mp_Items[0];
		auto f0 = [&p] { return *p; };
		DRound112Step(f1, f0) DRound112Step(f2, f1) DRound112Step(f3, f2) DRound112Step(f4, f3)
		DRound112Step(f5, f4) DRound112Step(f6, f5) DRound112Step(f7, f6) DRound112Step(f8, f7)
		DRound112Step(f9, f8) DRound112Step(f10, f9) DRound112Step(f11, f10) DRound112Step(f12, f11)
		co_await fg_Suspend();
		co_return f12();
	}
};

// Round 113: a helper that constructs an object that starts a phase starts it
namespace NRound113Helper
{
struct CApp : CDistributedAppActor {
  DMibActorEntries(StopApp) TCMap<int, int> mp_Items;
  DMibActorPhaseEntry(StopApp) void f_StopNow() {
    mp_State.m_bStoppingApp = true;
    mp_Items.f_Clear();
  }
  struct CStop {
    CStop(CApp *pThis) { pThis->f_StopNow(); }
  };
  void f_Helper() { CStop Stop(this); }
  TCFuture<int> f_Run() {
    int *pEntry = mp_Items.f_FindEqual(1);
    f_Helper();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension; 'mp_Items' loses entries only in StopApp, so a guard of that phase that is alive at the use, like fp_CheckStoppedOrDestroyedOnResume, makes it safe [malterlib-coroutine-actor-reference]
  }
};
}

// Round 113: a default member initializer of an object made in a resume callback runs
namespace NRound113DefaultInit
{
TCFuture<int> fg_Suspend();
struct CApp : CDistributedAppActor {
  DMibActorEntries(StopApp) TCMap<int, int> mp_Items;
  DMibActorPhaseEntry(StopApp) void f_StopNow() {
    mp_State.m_bStoppingApp = true;
    mp_Items.f_Clear();
  }
  struct CStop {
    CApp *m_pThis;
    int unused = (m_pThis->f_StopNow(), 0);
    CStop(CApp *pThis) : m_pThis(pThis) {}
  };
  TCFuture<int> f_Run() {
    auto Guard = co_await fg_OnResume([this]() -> CExceptionPointer {
      if (mp_State.m_bStoppingApp)
        return fg_MakeError("stopped");
      CStop Stop(this);
      return {};
    });
    int *pEntry = mp_Items.f_FindEqual(1);
    co_await fg_Suspend();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension; 'mp_Items' loses entries only in StopApp, so a guard of that phase that is alive at the use, like fp_CheckStoppedOrDestroyedOnResume, makes it safe [malterlib-coroutine-actor-reference]
  }
};
}

// Round 114: a reference that extends the result of fg_AsyncDestroy keeps it
namespace NRound114Extended
{
TCFuture<int> fg_Suspend();
struct A : CActor {
  DMibActorEntries(Owner) TCMap<CStr, int> items;
  TCFuture<int> f() {
    auto key = fg_RandomID(items);
    const auto &cleanup = co_await fg_AsyncDestroy([this, key]() -> TCFuture<int> { items.f_Remove(key); co_return 0; });
    int &entry = items[key];
    co_await fg_Suspend();
    co_return entry;
  }
};
}

// Round 114: separate guards alive together cover their phases
namespace NRound114Guards
{
TCFuture<int> fg_Suspend();
struct GuardAwaiter : std::suspend_never { COnResumeScope await_resume() noexcept; };
struct A : CDistributedAppActor {
  DMibActorEntries(Destroy, StopApp) TCMap<int, int> items;
  DMibActorPhaseGuard(Destroy) GuardAwaiter destroyGuard() const;
  DMibActorPhaseGuard(StopApp) GuardAwaiter stopGuard() const;
  TCFuture<int> f() {
    int *p = items.f_FindEqual(1);
    co_await fg_Suspend();
    auto destroyed = co_await destroyGuard();
    auto stopped = co_await stopGuard();
    co_return *p;
  }
};
}

// Round 119: a pointer assigned after its use is protected by the guard at
// the use, while it holds what it was made from.
namespace NRound119AssignedAfterUse
{
TCFuture<int> fg_Suspend();
struct CApp : CPhaseActor {
  DMibActorEntries(Destroy) TCMap<int, int> mp_Items;

  TCFuture<int> f_Run() {
    auto Guard = co_await f_CheckDestroyedOnResume();
    int *pEntry = &mp_Items[1];
    co_await fg_Suspend();
    int Result = *pEntry;
    pEntry = nullptr;
    co_return Result;
  }

  TCFuture<int> f_AssignedBefore(bool _bOther) {
    auto Guard = co_await f_CheckDestroyedOnResume();
    int *pEntry = &mp_Items[1];
    if (_bOther)
      pEntry = &mp_Items[2];
    co_await fg_Suspend();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension; 'mp_Items' loses entries only in Destroy, so a guard of that phase that is alive at the use, like f_CheckDestroyedOnResume, makes it safe [malterlib-coroutine-actor-reference]
  }
};
}

// Round 120: a lookup read through an offset of what a pointer to it points
// to, assigned to a static local or a parameter, and a pointer assigned after
// its use with deeply nested awaits.
namespace NRound120Pointers
{
TCFuture<int> fg_Suspend();
TCFuture<int> fg_Work(int _Value);
#define DRound120Await(d_Value) (co_await fg_Work(d_Value))
#define DRound120Await6(d_Value) DRound120Await(DRound120Await(DRound120Await(DRound120Await(DRound120Await(DRound120Await(d_Value))))))

struct CApp : CActor {
  TCMap<int, int> m_Items;

  TCFuture<int> f_Offset() {
    int *pEntry = &m_Items[0];
    int **ppEntry = &pEntry;
    co_await fg_Suspend();
    co_return *(*ppEntry + 0);
// CHECK-MESSAGES: :[[@LINE-1]]:18: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }

  TCFuture<int> f_Static() {
    static int *s_pEntry = nullptr;
    s_pEntry = m_Items.f_FindEqual(0);
    co_await fg_Suspend();
    co_return *s_pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 's_pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }

  TCUnsafeFuture<int> f_Parameter(int *_pEntry) {
    _pEntry = m_Items.f_FindEqual(0);
    co_await fg_Suspend();
    co_return *_pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: '_pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }

  TCFuture<int> f_Nested() {
    int *pEntry = &m_Items[0];
    int Value = *pEntry;
    int Awaited = DRound120Await6(DRound120Await6(DRound120Await6(DRound120Await6(0))));
    pEntry = nullptr;
    co_return Awaited + Value;
  }
};
}

// Round 121: a scope exit made by calling the operator explicitly, a result
// of `fg_OnResume` cleared before it is set, and what a lambda called in
// place returns through a local, not from a branch never taken.
namespace NRound121
{
TCFuture<int> fg_Suspend();
struct CApp : CPhaseActor {
  DMibActorEntries(Destroy) TCMap<int, int> m_Entries;
  TCMap<int, int> m_Items;

  TCFuture<int> f_Explicit() {
    auto *pEntry = m_Items.f_FindEqual(0);
    auto Cleanup = g_OnScopeExit.operator/([pEntry] { (void)*pEntry; });
// CHECK-MESSAGES: :[[@LINE-1]]:45: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
    co_await fg_Suspend();
    co_return 0;
  }

  TCFuture<int> f_ClearedBefore() {
    auto Guard = co_await fg_OnResume(
      [this]() -> TCAsyncResult<int> {
        if (f_IsDestroyed()) {
          TCAsyncResult<int> Result;
          Result.f_Clear();
          Result.f_SetResult(0);
          return Result;
        }
        return {};
      });
    auto &Entry = m_Entries[0];
    co_await fg_Suspend();
    co_return Entry;
  }

  TCFuture<int> f_Assigned() {
    int *pValue = [this] {
      int *pInner = nullptr;
      pInner = &m_Items[0];
      return pInner;
    }();
    co_await fg_Suspend();
    co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pValue' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }

  TCFuture<int> f_NeverTaken() {
    static int s_Global = 0;
    int *pValue = [this] {
      if (false)
        return &m_Items[0];
      return &s_Global;
    }();
    co_await fg_Suspend();
    co_return *pValue;
  }
};
}

// Round 122: a result returned many times is looked into once.
namespace NRound122ManyReturns
{
TCFuture<int> fg_Work();
#define DRound122Twice(d_Code) d_Code d_Code
struct CApp : CPhaseActor {
  DMibActorEntries(Destroy) TCMap<int, int> m_Items;

  TCFuture<int> f_Run(int _Count) {
    auto Guard = co_await fg_OnResume([this, _Count]() mutable {
      TCAsyncResult<int> Result;
      Result.f_SetResult(0);
      if (f_IsDestroyed()) {
        DRound122Twice(DRound122Twice(DRound122Twice(DRound122Twice(DRound122Twice(DRound122Twice(DRound122Twice(DRound122Twice(DRound122Twice(if (_Count--) return Result;)))))))))
        return Result;
      }
      return TCAsyncResult<int>{};
    });
    auto &Entry = m_Items[1];
    co_await fg_Work();
    co_return Entry;
  }
};
}

// Round 123: a lookup a call uses as it is, while its arguments suspend, is
// kept by the guards around it of the phases its entries are removed in, like
// a local reference to it; a guard awaited through a reference to its awaiter
// guards too.
namespace NRound123InlineGuard
{
struct CEntry {
  int f_Read(int) const;
};
TCFuture<int> fg_Work();
struct CApp : CDistributedAppActor {
  DMibActorEntries(StopApp) TCMap<int, CEntry> m_Items;

  TCFuture<int> f_Guarded() {
    auto Guard = co_await fp_CheckStoppedOrDestroyedOnResume();
    co_return m_Items[1].f_Read(co_await fg_Work());
  }

  TCFuture<int> f_Unguarded() {
    co_return m_Items[1].f_Read(co_await fg_Work());
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: the data of the actor that this call uses is looked up before its arguments suspend, during which other calls of the actor can change or erase the data; look it up after the suspension [malterlib-coroutine-actor-reference]
  }

  TCFuture<int> f_GuardEnded() {
    {
      auto Guard = co_await fp_CheckStoppedOrDestroyedOnResume();
    }
    co_return m_Items[1].f_Read(co_await fg_Work());
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: the data of the actor that this call uses is looked up before its arguments suspend, during which other calls of the actor can change or erase the data; look it up after the suspension [malterlib-coroutine-actor-reference]
  }

  TCFuture<int> f_OtherPhase() {
    auto Guard = co_await f_CheckDestroyedOnResume();
    co_return m_Items[1].f_Read(co_await fg_Work());
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: the data of the actor that this call uses is looked up before its arguments suspend, during which other calls of the actor can change or erase the data; look it up after the suspension [malterlib-coroutine-actor-reference]
  }

  TCFuture<int> f_AliasGuard() {
    auto Awaiter = fp_CheckStoppedOrDestroyedOnResume();
    auto &Alias = Awaiter;
    auto Guard = co_await fg_Move(Alias);
    auto &Entry = m_Items[1];
    co_await fg_Work();
    co_return Entry.f_Read(0);
  }
};
}

// Round 124: a callback registered after a guard, which runs after it on
// resume, can start the phase the guard guards, also for a lookup a call uses
// as it is; and a call on a final actor dispatches only to its own override.
namespace NRound124
{
TCFuture<int> fg_Suspend();
struct CItem {
  int f_Read(int);
};

struct CLaterStart : CDistributedAppActor {
  DMibActorEntries(StopApp) TCMap<int, CItem> mp_Items;
  bool m_bStopNow = false;

  DMibActorPhaseEntry(StopApp) void f_StopNow() {
    mp_State.m_bStoppingApp = true;
    mp_Items.f_Clear();
  }

  TCFuture<int> f_Run() {
    auto Guard = co_await fp_CheckStoppedOrDestroyedOnResume();
    auto Later = co_await fg_OnResume([this]() -> CExceptionPointer {
      if (m_bStopNow)
        f_StopNow();
      return {};
    });
    co_return mp_Items[1].f_Read(co_await fg_Suspend());
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: the data of the actor that this call uses is looked up before its arguments suspend, during which other calls of the actor can change or erase the data; look it up after the suspension [malterlib-coroutine-actor-reference]
  }
};

struct CBase : CDistributedAppActor {
  virtual void f_Tick() {}
};

struct COther : CBase {
  DMibActorPhaseEntry(StopApp) void f_Tick() override {
    mp_State.m_bStoppingApp = true;
  }
};

struct CFinal final : CBase {
  DMibActorEntries(StopApp) TCMap<int, int> mp_Items;

  TCFuture<int> f_Run() {
    auto Guard = co_await fg_OnResume([this]() -> CExceptionPointer {
      if (mp_State.m_bStoppingApp)
        return fg_MakeError("Stopped");
      f_Tick();
      return {};
    });
    auto *pEntry = mp_Items.f_FindEqual(1);
    co_await fg_Suspend();
    co_return *pEntry;
  }
};

struct CNotFinal : CBase {
  DMibActorEntries(StopApp) TCMap<int, int> mp_Items;

  TCFuture<int> f_Run() {
    auto Guard = co_await fg_OnResume([this]() -> CExceptionPointer {
      if (mp_State.m_bStoppingApp)
        return fg_MakeError("Stopped");
      f_Tick();
      return {};
    });
    auto *pEntry = mp_Items.f_FindEqual(1);
    co_await fg_Suspend();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension; 'mp_Items' loses entries only in StopApp, so a guard of that phase that is alive at the use, like fp_CheckStoppedOrDestroyedOnResume, makes it safe [malterlib-coroutine-actor-reference]
  }
};

struct CDerived : CNotFinal {
  DMibActorPhaseEntry(StopApp) void f_Tick() override {
    mp_State.m_bStoppingApp = true;
  }
};
}

// Round 125: destroying an aggregate, or the members and bases of what is
// destroyed, runs their destructors, which can start a phase.
namespace NRound125
{
TCFuture<int> fg_Suspend();

struct CApp : CDistributedAppActor {
  DMibActorEntries(StopApp) TCMap<int, int> mp_Items;
  bool m_bStopNow = false;

  DMibActorPhaseEntry(StopApp) void f_StopNow() {
    mp_State.m_bStoppingApp = true;
    mp_Items.f_Clear();
  }

  struct CStop {
    CApp *m_pThis;
    ~CStop() { m_pThis->f_StopNow(); }
  };
  struct CWrapper {
    CStop m_Stop;
  };
  struct CDerived : CStop {};

  TCFuture<int> f_Aggregate() {
    auto Guard = co_await fg_OnResume([this]() -> CExceptionPointer {
      if (mp_State.m_bStoppingApp)
        return fg_MakeError("Stopped");
      if (m_bStopNow) {
        CStop Stop{this};
      }
      return {};
    });
    auto &Entry = mp_Items[1];
    co_await fg_Suspend();
    co_return Entry;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'Entry' refers into the data of the actor and is used after a suspension; 'mp_Items' loses entries only in StopApp, so a guard of that phase that is alive at the use, like fp_CheckStoppedOrDestroyedOnResume, makes it safe [malterlib-coroutine-actor-reference]
  }

  TCFuture<int> f_Member() {
    auto &Entry = mp_Items[1];
    {
      CWrapper Stop{{this}};
    }
    co_return Entry;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'Entry' refers into the data of the actor and is used after a suspension; 'mp_Items' loses entries only in StopApp, so a guard of that phase that is alive at the use, like fp_CheckStoppedOrDestroyedOnResume, makes it safe [malterlib-coroutine-actor-reference]
  }

  TCFuture<int> f_Base() {
    auto &Entry = mp_Items[1];
    {
      CDerived Stop{{this}};
    }
    co_return Entry;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'Entry' refers into the data of the actor and is used after a suspension; 'mp_Items' loses entries only in StopApp, so a guard of that phase that is alive at the use, like fp_CheckStoppedOrDestroyedOnResume, makes it safe [malterlib-coroutine-actor-reference]
  }
};
}

// Round 126: a removal through a conditional, a comma or a cast of the
// collection removes the call's own entry, not one from a copy of the state,
// and an override in an instantiated function template can start a phase.
namespace NRound126
{
TCFuture<int> fg_Work();

struct COwnRemoval : CPhaseActor {
  DMibActorEntries(Owner) TCMap<CStr, int> mp_Items;

  TCFuture<int> f_Conditional(bool _bChoose) {
    auto ID = fg_RandomID(mp_Items);
    auto &Value = mp_Items[ID];
    (_bChoose ? mp_Items : mp_Items).f_Remove(ID);
    co_await fg_Work();
    co_return Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'Value' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }

  TCFuture<int> f_Comma() {
    auto ID = fg_RandomID(mp_Items);
    auto &Value = mp_Items[ID];
    (void(0), mp_Items).f_Remove(ID);
    co_await fg_Work();
    co_return Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'Value' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
};

struct CState {
  DMibActorEntries(Owner) TCMap<CStr, int> m_Items;
};

struct CCopyRemoval : CPhaseActor {
  CState mp_State;

  TCFuture<int> f_Copy() {
    auto ID = fg_RandomID(mp_State.m_Items);
    auto &Value = mp_State.m_Items[ID];
    auto Copy = mp_State;
    Copy.m_Items.f_Remove(ID);
    co_await fg_Work();
    co_return Value;
  }
};

struct CTemplateOverride : CDistributedAppActor {
  virtual void f_Tick() {}
  DMibActorEntries(Destroy, StopApp) TCMap<int, int> mp_Items;

  TCFuture<int> fp_StopApp() override {
    mp_Items.f_Clear();
    co_return 0;
  }

  TCFuture<int> f_Run() {
    auto Guard = co_await fp_CheckStoppedOrDestroyedOnResume();
    int &Entry = mp_Items[1];
    auto Resume = co_await fg_OnResume([this]() -> CExceptionPointer {
      f_Tick();
      return {};
    });
    co_await fg_Work();
    co_return Entry;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'Entry' refers into the data of the actor and is used after a suspension; 'mp_Items' loses entries only in Destroy and StopApp, so a guard of those phases that is alive at the use, like fp_CheckStoppedOrDestroyedOnResume, makes it safe [malterlib-coroutine-actor-reference]
  }
};

template <typename t_CBase>
t_CBase *fg_Make() {
  struct CDerived : t_CBase {
    void f_Tick() override { this->f_StopApp(); }
  };
  return new CDerived;
}
CTemplateOverride *fg_Factory() { return fg_Make<CTemplateOverride>(); }
}

// Round 128: registering a callback runs only it, so the guards alive don't
// check a phase that a call started since they last did, and a flag check
// whose condition can remove the entry after reading the flag doesn't check
// it.
namespace NRound128
{
struct CApp : CDistributedAppActor {
  DMibActorEntries(StopApp) TCMap<int, int> mp_Items;

  DMibActorPhaseEntry(StopApp) void f_StopNow() {
    mp_State.m_bStoppingApp = true;
    mp_Items.f_Clear();
  }

  TCFuture<int> f_Unrelated() {
    auto Guard = co_await fp_CheckStoppedOrDestroyedOnResume();
    int &Value = mp_Items[1];
    f_StopNow();
    auto Unrelated = co_await fg_OnResume([]() -> CExceptionPointer { return {}; });
    co_return Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'Value' refers into the data of the actor and is used after a suspension; 'mp_Items' loses entries only in StopApp, so a guard of that phase that is alive at the use, like fp_CheckStoppedOrDestroyedOnResume, makes it safe [malterlib-coroutine-actor-reference]
  }

  TCFuture<int> f_Rechecked() {
    auto Guard = co_await fp_CheckStoppedOrDestroyedOnResume();
    int &Value = mp_Items[1];
    f_StopNow();
    auto Again = co_await fp_CheckStoppedOrDestroyedOnResume();
    co_return Value;
  }
};

TCFuture<int> fg_Work();

struct CEntry {
  TCSharedPointer<bool> m_pDeleted{new bool(false)};
  int m_Value = 0;
  ~CEntry() { *m_pDeleted = true; }
};

struct CFlagged : CActor {
  DMibActorEntries(StopApp) TCMap<int, CEntry> mp_Items;
  DMibActorPhaseFlag(StopApp) bool mp_bStopping = false;

  DMibActorPhaseEntry(StopApp) void f_Stop() {
    mp_bStopping = true;
    mp_Items.f_Clear();
  }

  TCFuture<int> f_Read() {
    auto &Entry = mp_Items[0];
    auto pDeleted = Entry.m_pDeleted;
    co_await fg_Work();
    if (*pDeleted || (f_Stop(), false))
      co_return 0;
    co_return Entry.m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'Entry' refers into the data of the actor and is used after a suspension; 'mp_Items' loses entries only in StopApp, so a guard of that phase that is alive at the use, like fp_CheckStoppedOrDestroyedOnResume, makes it safe [malterlib-coroutine-actor-reference]
  }
};
}

// Round 128: an await in an arm of a conditional that a constant condition
// doesn't choose doesn't run.
namespace NRound128Unchosen
{
TCFuture<int> fg_Work();
struct CValue {
  int f_Read(int _Value) const;
};

struct CReader : CActor {
  TCMap<int, CValue> mp_Items;

  TCFuture<int> f_Unchosen() {
    co_return mp_Items[0].f_Read(false ? co_await fg_Work() : 1);
  }

  TCFuture<int> f_Chosen() {
    co_return mp_Items[0].f_Read(true ? co_await fg_Work() : 1);
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: the data of the actor that this call uses is looked up before its arguments suspend, during which other calls of the actor can change or erase the data; look it up after the suspension [malterlib-coroutine-actor-reference]
  }
};
}

// Round 129: a local reference to a flag of a phase reads the flag where a
// callback uses it.
namespace NRound129
{
TCFuture<int> fg_Suspend();

struct CApp : CActor {
  DMibActorPhaseFlag(Destroy) bool mp_bDestroyed = false;
  DMibActorEntries(Destroy) TCMap<int, int> mp_Items;

  TCFuture<int> f_Alias() {
    bool const &bDestroyed = mp_bDestroyed;
    auto Guard = co_await fg_OnResume([&bDestroyed]() -> CExceptionPointer {
      if (bDestroyed)
        return fg_MakeError("Destroyed");
      return {};
    });
    auto &Value = mp_Items[1];
    co_await fg_Suspend();
    co_return Value;
  }

  TCFuture<int> f_Copied() {
    bool const &bDestroyed = mp_bDestroyed;
    auto Guard = co_await fg_OnResume([bDestroyed]() -> CExceptionPointer {
      if (bDestroyed)
        return fg_MakeError("Destroyed");
      return {};
    });
    auto &Value = mp_Items[1];
    co_await fg_Suspend();
    co_return Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'Value' refers into the data of the actor and is used after a suspension; 'mp_Items' loses entries only in Destroy, so a guard of that phase that is alive at the use, like f_CheckDestroyedOnResume, makes it safe [malterlib-coroutine-actor-reference]
  }
};
}

// Round 130: a member through a lookup moved by an offset that suspends is
// accessed after it.
namespace NRound130
{
TCFuture<int> fg_Offset();
struct CItem {
  int m_Value;
};

struct CApp : CActor {
  TCMap<int, CItem> mp_Items;

  TCFuture<int> f_MemberRead() {
    auto *pItem = mp_Items.f_FindEqual(1);
    co_return (pItem + co_await fg_Offset())->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pItem' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
};
}

// Round 131: a reference bound to itself tells nothing, and a future of a
// guard assigned another before it is awaited is that other.
namespace NRound131
{
struct CApp : CDistributedAppActor {
  TCFuture<int> f_SelfBound() {
    bool const &bFlag = bFlag;
    auto Registration = co_await fg_OnResume([&bFlag]() -> CExceptionPointer {
      if (bFlag)
        return fg_MakeError("Stopped");
      return {};
    });
    co_return 0;
  }
};

TCFuture<COnResumeScope> fg_Later();

struct CReplaced : CActor {
  TCMap<int, int> mp_Items;
  DMibActorPhaseGuard(Destroy) TCFuture<COnResumeScope> f_Guard();

  TCFuture<int> f_Run() {
    int *pValue = &mp_Items[0];
    auto Pending = f_Guard();
    Pending = fg_Later();
    auto Scope = co_await fg_Move(Pending);
    co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pValue' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
};
}

// Round 132: an override beside the class that shares the class declaring the
// function as a virtual base can run, an assignment through a pointer to a
// pointer is where it is written, also read through more of them, and what
// a callback looks up again on resume isn't what was read before.
namespace NRound132
{
TCFuture<int> fg_Suspend();

struct CBase : CPhaseActor {
  DMibActorEntries(Destroy) TCMap<int, int> mp_Items;
  virtual void f_Tick() {}
  TCFuture<int> fp_Destroy() override {
    mp_Items.f_Clear();
    co_return 0;
  }
};

struct CApp : virtual CBase {
  TCFuture<int> f_Run() {
    auto Guard = co_await f_CheckDestroyedOnResume();
    int *pValue = mp_Items.f_FindEqual(1);
    co_await fg_Suspend();
    f_Tick();
    co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pValue' refers into the data of the actor and is used after a suspension; 'mp_Items' loses entries only in Destroy, so a guard of that phase that is alive at the use, like f_CheckDestroyedOnResume, makes it safe [malterlib-coroutine-actor-reference]
  }
};

struct CSide : virtual CBase {
  void f_Tick() override { (void)fp_DestroyInternal(); }
};
struct CCombined : CApp, CSide {};

struct CPointers : CActor {
  TCMap<int, int> mp_Items;

  TCFuture<int> f_Later(int _Local) {
    int *pValue = &mp_Items[1];
    int **ppValue = &pValue;
    co_await fg_Suspend();
    int Result = *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:19: warning: 'pValue' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
    *ppValue = &_Local;
    co_return Result;
  }

  TCFuture<int> f_Refreshed() {
    int *pValue = &mp_Items[1];
    int **ppValue = &pValue;
    co_await fg_Suspend();
    *ppValue = &mp_Items[1];
    co_return *pValue;
  }

  TCFuture<int> f_Three() {
    int *pValue = &mp_Items[1];
    int **ppValue = &pValue;
    int ***pppValue = &ppValue;
    co_await fg_Suspend();
    co_return ***pppValue;
// CHECK-MESSAGES: :[[@LINE-1]]:18: warning: 'pValue' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
};

struct CValue {
  int m_Value;
  int f_Read(int _Add) const { return m_Value + _Add; }
};

struct CReceiver : CActor {
  TCMap<int, CValue> mp_Items;

  TCFuture<int> f_Run() {
    CValue *pValue = mp_Items.f_FindEqual(1);
    auto Guard = co_await fg_OnResume([&]() -> CExceptionPointer {
      pValue = mp_Items.f_FindEqual(1);
      return pValue ? CExceptionPointer{} : fg_MakeError("Gone");
    });
    co_return pValue->f_Read(co_await fg_Suspend());
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pValue' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
};
}

// Round 136: a helper that puts back the address it is given by reference
// doesn't look up again; testing an iterator doesn't re-validate it; looking
// a pointer up again doesn't re-validate what was bound to what it held
// before; and a cleanup run early starts its phases.
namespace NRound136
{
TCFuture<int> fg_Work();

void fg_Keep(int *&_pValue, int &_Value) { _pValue = &_Value; }

struct CKeep : CActor {
  TCMap<int, int> mp_Items;

  TCFuture<int> f_Read() {
    int *pValue = mp_Items.f_FindEqual(0);
    auto Resume = co_await fg_OnResume([&] {
      fg_Keep(pValue, *pValue);
// CHECK-MESSAGES: :[[@LINE-1]]:24: warning: 'pValue' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
      return CExceptionPointer{};
    });
    co_await fg_Work();
    co_return *pValue;
  }
};

struct CIterated : CActor {
  TCMap<int, int> mp_Items;

  TCFuture<int> f_Read() {
    auto iEntry = mp_Items.f_GetIterator();
    auto Resume = co_await fg_OnResume([&] {
      if (!iEntry)
        return fg_MakeError("Missing entry");
      return CExceptionPointer{};
    });
    co_await fg_Work();
    co_return *iEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'iEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
};

struct CReassigned : CActor {
  TCMap<int, int> mp_Items;

  TCFuture<int> f_Read() {
    int *pValue = mp_Items.f_FindEqual(1);
    int &Value = *pValue;
    pValue = mp_Items.f_FindEqual(2);
    auto Guard = co_await fg_OnResume([this, &pValue] {
      if (pValue != mp_Items.f_FindEqual(2))
        return fg_MakeError("Gone");
      return CExceptionPointer{};
    });
    co_await fg_Work();
    co_return Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'Value' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
};

struct CEarly : CActor {
  DMibActorPhaseFlag(StopApp) bool mp_bStopped = false;
  DMibActorEntries(StopApp) TCMap<int, int> mp_Items;
  DMibActorPhaseGuard(StopApp) TCFuture<COnResumeScope> f_Guard();

  DMibActorPhaseEntry(StopApp) void f_Stop() {
    mp_bStopped = true;
    mp_Items.f_Clear();
  }

  TCFuture<int> f_Run() {
    auto Guard = co_await f_Guard();
    int *pValue = mp_Items.f_FindEqual(1);
    auto Cleanup = g_OnScopeExit / [this] { f_Stop(); };
    Cleanup();
    co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pValue' refers into the data of the actor and is used after a suspension; 'mp_Items' loses entries only in StopApp, so a guard of that phase that is alive at the use, like fp_CheckStoppedOrDestroyedOnResume, makes it safe [malterlib-coroutine-actor-reference]
  }
};
}

namespace NRound137 {
TCFuture<int> fg_Work();

// A reference to a pointer in an entry, `auto const &pValue = mp_Items[0];`,
// reads the entry when it is compared after a suspension.
struct CApp : CActor {
  TCMap<int, int *> mp_Items;

  TCFuture<int> f_Compare() {
    auto const &pValue = mp_Items[0];
    co_await fg_Work();
    co_return pValue != nullptr;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pValue' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }

  // But a copy of it doesn't.
  TCFuture<int> f_Copy() {
    int *pValue = mp_Items[0];
    co_await fg_Work();
    co_return pValue != nullptr;
  }
};
}

namespace NRound138 {
TCFuture<int> fg_Work();

struct CEntry {
  TCSharedPointer<bool> m_pDeleted;
  int m_Value;
};

// A test of a removal flag whose condition can remove the entry, `*pDeleted ||
// (mp_Items.f_Clear(), false)`, or after which the entry can be removed, `if
// (*pDeleted) return 0; mp_Items.f_Clear();`, doesn't validate it.
struct CApp : CActor {
  TCMap<int, CEntry> mp_Items;

  TCFuture<int> f_Condition() {
    auto *pEntry = mp_Items.f_FindEqual(1);
    if (!pEntry)
      co_return 0;
    auto pDeleted = pEntry->m_pDeleted;
    co_await fg_Work();
    co_return [this, pEntry, pDeleted] {
// CHECK-MESSAGES: :[[@LINE-1]]:22: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
      if (*pDeleted || (mp_Items.f_Clear(), false))
        return 0;
      return pEntry->m_Value;
    }();
  }

  TCFuture<int> f_After() {
    auto *pEntry = mp_Items.f_FindEqual(1);
    if (!pEntry)
      co_return 0;
    auto pDeleted = pEntry->m_pDeleted;
    co_await fg_Work();
    co_return [this, pEntry, pDeleted] {
// CHECK-MESSAGES: :[[@LINE-1]]:22: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
      if (*pDeleted)
        return 0;
      mp_Items.f_Clear();
      return pEntry->m_Value;
    }();
  }

  // But one only followed by reads of the actor does.
  TCFuture<int> f_Read() {
    auto *pEntry = mp_Items.f_FindEqual(1);
    if (!pEntry)
      co_return 0;
    auto pDeleted = pEntry->m_pDeleted;
    co_await fg_Work();
    co_return [this, pEntry, pDeleted] {
      if (*pDeleted)
        return 0;
      return pEntry->m_Value + (mp_Items.f_FindEqual(2) ? 1 : 0);
    }();
  }
};
}

namespace NRound139 {
TCFuture<int> fg_Work();

// A lookup in either of two collections, `(bChoose ? mp_First :
// mp_Second)[1]`, is safe only when both lose entries in the phases of the
// guards.
struct CApp : CDistributedAppActor {
  DMibActorEntries(Destroy) TCMap<int, int> mp_First;
  DMibActorEntries(StopApp) TCMap<int, int> mp_Second;
  DMibActorEntries(Destroy) TCMap<int, int> mp_Third;
  TCMap<int, int> mp_Plain;

  TCFuture<int> f_Other(bool _bChoose) {
    auto Guard = co_await f_CheckDestroyedOnResume();
    auto &Value = (_bChoose ? mp_First : mp_Second)[1];
    co_await fg_Work();
    co_return Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'Value' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }

  TCFuture<int> f_Plain(bool _bChoose) {
    auto Guard = co_await f_CheckDestroyedOnResume();
    auto &Value = (_bChoose ? mp_First : mp_Plain)[1];
    co_await fg_Work();
    co_return Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'Value' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }

  TCFuture<int> f_Same(bool _bChoose) {
    auto Guard = co_await f_CheckDestroyedOnResume();
    auto &Value = (_bChoose ? mp_First : mp_Third)[1];
    co_await fg_Work();
    co_return Value;
  }
};
}

namespace NRound141 {
TCFuture<int> fg_Work();

// The default arguments that use the one before, which their uses share, are
// looked at once.
int fg_Chain0(int = 0);
int fg_Chain1(int = fg_Chain0() + fg_Chain0());
int fg_Chain2(int = fg_Chain1() + fg_Chain1());
int fg_Chain3(int = fg_Chain2() + fg_Chain2());
int fg_Chain4(int = fg_Chain3() + fg_Chain3());
int fg_Chain5(int = fg_Chain4() + fg_Chain4());
int fg_Chain6(int = fg_Chain5() + fg_Chain5());
int fg_Chain7(int = fg_Chain6() + fg_Chain6());
int fg_Chain8(int = fg_Chain7() + fg_Chain7());
int fg_Chain9(int = fg_Chain8() + fg_Chain8());
int fg_Chain10(int = fg_Chain9() + fg_Chain9());
int fg_Chain11(int = fg_Chain10() + fg_Chain10());
int fg_Chain12(int = fg_Chain11() + fg_Chain11());
int fg_Chain13(int = fg_Chain12() + fg_Chain12());
int fg_Chain14(int = fg_Chain13() + fg_Chain13());
int fg_Chain15(int = fg_Chain14() + fg_Chain14());
int fg_Chain16(int = fg_Chain15() + fg_Chain15());
int fg_Chain17(int = fg_Chain16() + fg_Chain16());
int fg_Chain18(int = fg_Chain17() + fg_Chain17());
int fg_Chain19(int = fg_Chain18() + fg_Chain18());
int fg_Chain20(int = fg_Chain19() + fg_Chain19());
int fg_Chain21(int = fg_Chain20() + fg_Chain20());
int fg_Chain22(int = fg_Chain21() + fg_Chain21());
int fg_Chain23(int = fg_Chain22() + fg_Chain22());
int fg_Chain24(int = fg_Chain23() + fg_Chain23());
int fg_Chain25(int = fg_Chain24() + fg_Chain24());
int fg_Chain26(int = fg_Chain25() + fg_Chain25());
int fg_Chain27(int = fg_Chain26() + fg_Chain26());
int fg_Chain28(int = fg_Chain27() + fg_Chain27());
int fg_Chain29(int = fg_Chain28() + fg_Chain28());
int fg_Chain30(int = fg_Chain29() + fg_Chain29());
int fg_Chain31(int = fg_Chain30() + fg_Chain30());
int fg_Chain32(int = fg_Chain31() + fg_Chain31());
int fg_Chain33(int = fg_Chain32() + fg_Chain32());
int fg_Chain34(int = fg_Chain33() + fg_Chain33());
int fg_Chain35(int = fg_Chain34() + fg_Chain34());
int fg_Chain36(int = fg_Chain35() + fg_Chain35());
int fg_Chain37(int = fg_Chain36() + fg_Chain36());
int fg_Chain38(int = fg_Chain37() + fg_Chain37());
int fg_Chain39(int = fg_Chain38() + fg_Chain38());
int fg_Chain40(int = fg_Chain39() + fg_Chain39());

struct CApp : CDistributedAppActor {
  DMibActorEntries(StopApp) TCMap<int, int> mp_Items;

  TCFuture<int> f_Chain() {
    auto Guard = co_await fg_OnResume([this]() -> CExceptionPointer {
      if (mp_State.m_bStoppingApp)
        return fg_MakeError("Stopping");
      fg_Chain40();
      return {};
    });
    auto &Entry = mp_Items[1];
    co_await fg_Work();
    co_return Entry;
  }
};
}

namespace NRound142 {
TCFuture<int> fg_Work();

namespace NAdapter {
struct CExceptionPtr {
  CExceptionPtr();
};
template <typename tf_CError>
CExceptionPtr fg_MakeExceptionPtr(tf_CError _Error);
}
}
namespace std {
using exception_ptr = NRound142::NAdapter::CExceptionPtr;
template <typename tf_CError>
exception_ptr make_exception_ptr(tf_CError _Error);
template <typename tf_FFunction>
auto invoke(tf_FFunction &&_fFunction) -> decltype(_fFunction());
}
namespace NMib::NConcurrency {
template <typename tf_FCallback>
  requires requires(tf_FCallback &_fCallback) {
    requires __is_same(decltype(_fCallback()), std::exception_ptr);
  }
TCFuture<COnResumeScope> fg_OnResume(tf_FCallback &&_fCallback);
}
namespace NRound142 {
// A callback that makes an exception pointer, `std::make_exception_ptr(...)`,
// when the phase has begun, aborts.
struct CExceptions : CPhaseActor {
  DMibActorEntries(Destroy) TCMap<int, int> mp_Items;

  TCFuture<int> f_Run() {
    auto Guard = co_await fg_OnResume([this]() -> std::exception_ptr {
      if (f_IsDestroyed())
        return std::make_exception_ptr(1);
      return {};
    });
    auto &Entry = mp_Items[0];
    co_await fg_Work();
    co_return Entry;
  }

  // Also after a branch that never runs, `if (false) return {};`.
  TCFuture<int> f_Dead() {
    auto &Entry = mp_Items[0];
    auto Guard = co_await fg_OnResume([this]() -> CExceptionPointer {
      if (false)
        return {};
      if (f_IsDestroyed())
        return fg_MakeError("Destroyed");
      return {};
    });
    co_await fg_Work();
    co_return Entry;
  }
};

// The default arguments that use the one before, which their uses share, are
// looked at once in a helper.
int fg_Chain0(int = 0);
int fg_Chain1(int = fg_Chain0() + fg_Chain0());
int fg_Chain2(int = fg_Chain1() + fg_Chain1());
int fg_Chain3(int = fg_Chain2() + fg_Chain2());
int fg_Chain4(int = fg_Chain3() + fg_Chain3());
int fg_Chain5(int = fg_Chain4() + fg_Chain4());
int fg_Chain6(int = fg_Chain5() + fg_Chain5());
int fg_Chain7(int = fg_Chain6() + fg_Chain6());
int fg_Chain8(int = fg_Chain7() + fg_Chain7());
int fg_Chain9(int = fg_Chain8() + fg_Chain8());
int fg_Chain10(int = fg_Chain9() + fg_Chain9());
int fg_Chain11(int = fg_Chain10() + fg_Chain10());
int fg_Chain12(int = fg_Chain11() + fg_Chain11());
int fg_Chain13(int = fg_Chain12() + fg_Chain12());
int fg_Chain14(int = fg_Chain13() + fg_Chain13());
int fg_Chain15(int = fg_Chain14() + fg_Chain14());
int fg_Chain16(int = fg_Chain15() + fg_Chain15());
int fg_Chain17(int = fg_Chain16() + fg_Chain16());
int fg_Chain18(int = fg_Chain17() + fg_Chain17());
int fg_Chain19(int = fg_Chain18() + fg_Chain18());
int fg_Chain20(int = fg_Chain19() + fg_Chain19());
int fg_Chain21(int = fg_Chain20() + fg_Chain20());
int fg_Chain22(int = fg_Chain21() + fg_Chain21());
int fg_Chain23(int = fg_Chain22() + fg_Chain22());
int fg_Chain24(int = fg_Chain23() + fg_Chain23());
int fg_Chain25(int = fg_Chain24() + fg_Chain24());
int fg_Chain26(int = fg_Chain25() + fg_Chain25());
int fg_Chain27(int = fg_Chain26() + fg_Chain26());
int fg_Chain28(int = fg_Chain27() + fg_Chain27());
int fg_Chain29(int = fg_Chain28() + fg_Chain28());
int fg_Chain30(int = fg_Chain29() + fg_Chain29());
int fg_Chain31(int = fg_Chain30() + fg_Chain30());
int fg_Chain32(int = fg_Chain31() + fg_Chain31());
int fg_Chain33(int = fg_Chain32() + fg_Chain32());
int fg_Chain34(int = fg_Chain33() + fg_Chain33());
int fg_Chain35(int = fg_Chain34() + fg_Chain34());
int fg_Chain36(int = fg_Chain35() + fg_Chain35());
int fg_Chain37(int = fg_Chain36() + fg_Chain36());
int fg_Chain38(int = fg_Chain37() + fg_Chain37());
int fg_Chain39(int = fg_Chain38() + fg_Chain38());
int fg_Chain40(int = fg_Chain39() + fg_Chain39());

struct CHelper : CPhaseActor {
  DMibActorEntries(Destroy) TCMap<int, int> mp_Items;
  void f_Helper() { (void)fg_Chain40(); }

  TCFuture<int> f_Run() {
    auto Guard = co_await f_CheckDestroyedOnResume();
    auto &Entry = mp_Items[0];
    f_Helper();
    co_await fg_Work();
    co_return Entry;
  }
};

// What a lambda that `std::invoke` runs starts.
struct CInvoked : CDistributedAppActor {
  DMibActorEntries(StopApp) TCMap<int, int> mp_Items;
  DMibActorPhase(StopApp) TCFuture<int> fp_StopApp() override {
    mp_Items.f_Clear();
    co_return 0;
  }

  TCFuture<int> f_Run() {
    auto Guard = co_await fp_CheckStoppedOrDestroyedOnResume();
    auto &Entry = mp_Items[0];
    co_await fg_Work();
    std::invoke([this] { return f_StopApp(); });
    co_return Entry;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'Entry' refers into the data of the actor and is used after a suspension; 'mp_Items' loses entries only in StopApp, so a guard of that phase that is alive at the use, like fp_CheckStoppedOrDestroyedOnResume, makes it safe [malterlib-coroutine-actor-reference]
  }
};

// A removal flag tested, after which the entry is removed, `mp_Items
// .f_Clear()`.
struct CEntry {
  int m_Value;
  TCSharedPointer<bool> m_pDeleted;
};

struct CRemoved : CActor {
  TCMap<int, CEntry> mp_Items;

  TCFuture<int> f_Removed() {
    auto &Entry = mp_Items[0];
    auto pDeleted = Entry.m_pDeleted;
    co_await fg_Work();
    if (*pDeleted)
      co_return 0;
    mp_Items.f_Clear();
    co_return Entry.m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'Entry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }

  TCFuture<int> f_Kept() {
    auto &Entry = mp_Items[0];
    auto pDeleted = Entry.m_pDeleted;
    co_await fg_Work();
    if (*pDeleted)
      co_return 0;
    co_return Entry.m_Value;
  }
};
}

namespace NRound146 {
TCFuture<int> fg_Suspend();

struct CQueued : CActor
{
	TCMap<int, int> mp_Items;

	// A dispatched lambda that refreshes the pointer only runs later, so the
	// pointer is still stale where it is read after.
	TCFuture<int> f_Queued()
	{
		auto *pEntry = &mp_Items[0];
		co_await fg_Suspend();
		auto Pending = g_Dispatch / [this, &pEntry]
			{
				pEntry = &mp_Items[0];
				return 0;
			}
		;
		int Value = *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
		co_await fg_Move(Pending);
		co_return Value;
	}

	// The pointer a reversed subscript indexes, written last, is looked up
	// after its index suspends.
	TCFuture<int> f_Reversed()
	{
		co_return (co_await fg_Suspend(), 0)[&mp_Items[0]];
	}
};
}

namespace NRound147 {
TCFuture<int> fg_Suspend();

// An override in a base of the class of the object, which a using
// declaration names the function past, runs.
struct CBase : CActor
{
	DMibActorEntries(Destroy) TCMap<int, int> mp_Items;
	DMibActorPhaseFlag(Destroy) bool mp_bDestroyed = false;
	DMibActorPhaseGuard(Destroy) TCFuture<COnResumeScope> f_Guard();

	DMibActorPhaseEntry(Destroy) void f_DestroyNow()
	{
		mp_bDestroyed = true;
		mp_Items.f_Clear();
	}
	virtual void f_Tick() {}
};

struct CMid : CBase
{
	void f_Tick() override
	{
		f_DestroyNow();
	}
};

struct CApp final : CMid
{
	using CBase::f_Tick;
	TCFuture<int> f_Run()
	{
		auto Guard = co_await f_Guard();
		int *pValue = mp_Items.f_FindEqual(1);
		co_await fg_Suspend();
		f_Tick();
		co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: 'pValue' refers into the data of the actor and is used after a suspension; 'mp_Items' loses entries only in Destroy, so a guard of that phase that is alive at the use, like f_CheckDestroyedOnResume, makes it safe [malterlib-coroutine-actor-reference]
	}
};

// A chain of default arguments whose first has a temporary that is only
// tracked for its lifetime is looked at in time linear in its length.
template <int t_Depth>
struct TCChain
{
	static int fs_Get(int _Value = TCChain<t_Depth - 1>::fs_Get() + TCChain<t_Depth - 1>::fs_Get());
};
template <>
struct TCChain<0>
{
	static int fs_Get(const int &_Value = 0);
};
struct CTemporary
{
	~CTemporary();
};
CTemporary fg_Sink(int _Value);

struct CChained : CActor
{
	TCMap<int, int> mp_Items;
	TCFuture<int> f_Run()
	{
		int *pValue = mp_Items.f_FindEqual(1);
		fg_Sink(TCChain<40>::fs_Get());
		co_await fg_Suspend();
		co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: 'pValue' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
	}
};
}

namespace NRound148 {
TCFuture<int> fg_Suspend();

void fg_Refresh(int *&_pValue, TCMap<int, int> &_Items)
{
	int *&pAlias = _pValue;
	pAlias = _Items.f_FindEqual(1);
}

struct CApp : CActor
{
	TCMap<int, int> mp_Items;

	// A pointer kept from before, moved back, isn't looked up again.
	TCFuture<int> f_Moved()
	{
		int *pValue = mp_Items.f_FindEqual(1);
		int *pSaved = pValue;
		auto Guard = co_await fg_OnResume
			(
				[&]() -> CExceptionPointer
				{
					pValue = fg_Move(pSaved);
					if (!pValue)
						return fg_MakeError("Gone");
					return {};
				}
			)
		;
		co_await fg_Suspend();
		co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: 'pValue' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
	}

	// A lookup in a block of the callback runs each time it does.
	TCFuture<int> f_Block()
	{
		int *pValue = mp_Items.f_FindEqual(1);
		auto Guard = co_await fg_OnResume
			(
				[&]() -> CExceptionPointer
				{
					{
						pValue = mp_Items.f_FindEqual(1);
					}
					if (!pValue)
						return fg_MakeError("Gone");
					return {};
				}
			)
		;
		co_await fg_Suspend();
		co_return *pValue;
	}

	// And in a helper, through a local reference to its parameter.
	TCFuture<int> f_Helper()
	{
		int *pValue = mp_Items.f_FindEqual(1);
		auto Guard = co_await fg_OnResume
			(
				[&]() -> CExceptionPointer
				{
					fg_Refresh(pValue, mp_Items);
					if (!pValue)
						return fg_MakeError("Gone");
					return {};
				}
			)
		;
		co_await fg_Suspend();
		co_return *pValue;
	}

	// The address of the pointer, or a reference to it, doesn't read it.
	TCFuture<int> f_Address()
	{
		auto *pValue = &mp_Items[1];
		co_await fg_Suspend();
		auto **ppValue = &pValue;
		int *&pAlias = pValue;
		pAlias = &mp_Items[1];
		co_return *pAlias + (*ppValue != nullptr);
	}
};
}

namespace NRound151 {
TCFuture<int> fg_Work();

struct CApp : CActor
{
	TCMap<int, int> mp_Items;

	// An iterator assigned through a local reference to it.
	TCFuture<int> f_IteratorAlias()
	{
		TCMap<int, int>::CIterator iEntry;
		auto &iAlias = iEntry;
		iAlias = mp_Items.f_GetIterator(1);
		co_await fg_Work();
		co_return *iEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: 'iEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
	}

	// A callback that removes the entries through `operator=` by name.
	TCFuture<int> f_NamedAssign()
	{
		auto pValue = mp_Items.f_FindEqual(1);
		auto Guard = co_await fg_OnResume
			(
				[&]() -> CExceptionPointer
				{
					pValue = mp_Items.f_FindEqual(1);
					mp_Items.operator=(TCMap<int, int>{});
					return {};
				}
			)
		;
		co_await fg_Work();
		co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: 'pValue' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
	}

	// The address of a local reference to a local pointer is of the pointer.
	TCFuture<int> f_AliasAddress()
	{
		auto *pValue = mp_Items.f_FindEqual(1);
		auto *&pAlias = pValue;
		auto **ppValue = &pAlias;
		co_await fg_Work();
		pValue = mp_Items.f_FindEqual(1);
		co_return **ppValue;
	}
};
}

namespace NRound152 {
TCFuture<int> fg_Work();

struct CMap
{
	int &operator[](int _Key);
};

// A reference to what an assignment changes.
struct CApp : CActor
{
	CMap mp_Items;
	TCFuture<int> f_Assigned()
	{
		int &Value = (mp_Items[0] = 1);
		co_await fg_Work();
		co_return Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'Value' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
	}
};
}

namespace NRound153 {
TCFuture<int> fg_Suspend();

// A getter that returns a local assigned what refers into the actor.
struct CAssignedGetter : CActor {
  TCMap<int, int> mp_Items;
  int *f_Get() {
    int *pValue = nullptr;
    pValue = mp_Items.f_FindEqual(0);
    return pValue;
  }
  TCFuture<int> f_Run() {
    int *pValue = f_Get();
    co_await fg_Suspend();
    co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pValue' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
};

// What a getter returns only in a branch that is never taken tells nothing.
struct CDiscardedReturn : CActor {
  TCMap<int, int> mp_Items;
  int m_Value;
  int *f_Get() {
    if constexpr (false)
      return mp_Items.f_FindEqual(0);
    return &m_Value;
  }
  TCFuture<int> f_Run() {
    int *pValue = f_Get();
    co_await fg_Suspend();
    co_return *pValue;
  }
};

// A getter called through an alias of this.
struct CThisAlias : CActor {
  int m_Value;
  int *f_Get() { return &m_Value; }
  TCFuture<int> f_Run() {
    auto *pThis = this;
    int *pValue = pThis->f_Get();
    co_await fg_Suspend();
    co_return *pValue;
  }
};

// A collection cleared through a local alias of it after the removal flag is
// checked.
struct CEntry {
  TCSharedPointer<bool> m_pDeleted;
  int m_Value;
  ~CEntry() { *m_pDeleted = true; }
};
struct CAliasCleared : CActor {
  TCMap<int, CEntry> mp_Items;
  TCFuture<int> f_Run() {
    auto &Items = mp_Items;
    auto *pEntry = Items.f_FindEqual(1);
    auto pDeleted = pEntry->m_pDeleted;
    co_await CAwaiter{};
    if (*pDeleted)
      co_return 0;
    Items.f_Clear();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
};
}

namespace NRound153 {
// What a call that only links or unlinks can't remove, but one that deletes,
// or calls what can't be followed, can.
struct CLink {
  CLink *m_pNext = nullptr;
};
struct CIntrusiveList {
  CLink *m_pFirst = nullptr;
  void f_Remove(CLink *_pLink) {
    if (m_pFirst == _pLink)
      m_pFirst = _pLink->m_pNext;
  }
};
struct COwningList {
  CLink *m_pFirst = nullptr;
  void f_Remove(CLink *_pLink) {
    if (m_pFirst == _pLink)
      m_pFirst = _pLink->m_pNext;
    delete _pLink;
  }
};
struct CVirtualList {
  virtual void f_Remove(CLink *_pLink);
};
struct CLinkedEntry {
  TCSharedPointer<bool> m_pDeleted;
  CLink m_Link;
  int m_Value;
  ~CLinkedEntry() { *m_pDeleted = true; }
};
struct CLinked : CActor {
  TCMap<int, CLinkedEntry> mp_Items;
  CIntrusiveList mp_Intrusive;
  COwningList mp_Owning;
  CVirtualList mp_Virtual;
  TCFuture<int> f_Unlinked() {
    auto *pEntry = mp_Items.f_FindEqual(1);
    auto pDeleted = pEntry->m_pDeleted;
    co_await CAwaiter{};
    if (*pDeleted)
      co_return 0;
    auto &Intrusive = mp_Intrusive;
    Intrusive.f_Remove(&pEntry->m_Link);
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_Deleted() {
    auto *pEntry = mp_Items.f_FindEqual(1);
    auto pDeleted = pEntry->m_pDeleted;
    co_await CAwaiter{};
    if (*pDeleted)
      co_return 0;
    mp_Owning.f_Remove(&pEntry->m_Link);
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_Virtual() {
    auto *pEntry = mp_Items.f_FindEqual(1);
    auto pDeleted = pEntry->m_pDeleted;
    co_await CAwaiter{};
    if (*pDeleted)
      co_return 0;
    mp_Virtual.f_Remove(&pEntry->m_Link);
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
};
}

namespace NRound154 {
// A free function given the data to change.
struct CEntry {
  TCSharedPointer<bool> m_pDeleted;
  int m_Value = 0;
  ~CEntry() { *m_pDeleted = true; }
};
void fg_Clear(TCMap<int, CEntry> &_Items) { _Items.f_Clear(); }
int fg_Count(TCMap<int, CEntry> const &_Items);
struct CApp : CActor {
  TCMap<int, CEntry> mp_Items;
  TCFuture<int> f_Cleared() {
    auto *pEntry = mp_Items.f_FindEqual(0);
    auto pDeleted = pEntry->m_pDeleted;
    co_await CAwaiter{};
    if (*pDeleted)
      co_return 0;
    fg_Clear(mp_Items);
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  // Given it to read, it can't remove anything.
  TCFuture<int> f_Counted() {
    auto *pEntry = mp_Items.f_FindEqual(0);
    auto pDeleted = pEntry->m_pDeleted;
    co_await CAwaiter{};
    if (*pDeleted)
      co_return 0;
    fg_Count(mp_Items);
    co_return pEntry->m_Value;
  }
};
}

namespace NRound157 {
TCFuture<int> fg_Suspend();
struct CEntry {
  int m_Value;
};
struct CApp : CActor {
  TCMap<int, CEntry> mp_Items;
  TCMap<int, int> mp_Values;
  // The address of a structured binding of a lookup.
  TCFuture<int> f_Binding() {
    auto &[Value] = mp_Items[0];
    int *pValue = &Value;
    co_await fg_Suspend();
    co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pValue' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  // Re-validated by a callback that returns success on a match first.
  TCFuture<int> f_SuccessFirst() {
    int *pValue = mp_Values.f_FindEqual(1);
    if (!pValue)
      co_return 0;
    auto OnResume = co_await fg_OnResume([this, &pValue]() -> CExceptionPointer {
      if (pValue == mp_Values.f_FindEqual(1))
        return {};
      return fg_MakeError("Gone");
    });
    co_await CAwaiter{};
    co_return *pValue;
  }
};
}

namespace NRound158 {
TCFuture<int> fg_Work();
struct CValue {
  int m_X;
};
// The address of a member of what a helper is given.
int *fg_Project(CValue *_pValue) { return &_pValue->m_X; }
// A helper that gives only what has static storage.
char const *fg_Label(TCMap<int, int> const &) { return "Fixed"; }
struct CApp : CActor {
  TCMap<int, CValue> mp_Items;
  TCMap<int, int> mp_Values;
  TCFuture<int> f_Projected() {
    int *pValue = fg_Project(mp_Items.f_FindEqual(0));
    co_await fg_Work();
    co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pValue' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_Label() {
    char const *pLabel = fg_Label(mp_Values);
    co_await fg_Work();
    co_return *pLabel;
  }
};
}

namespace NRound159 {
TCFuture<int> fg_Work();
struct CApp : CActor {
  TCMap<int, int> mp_Items;
  // A copy that a lambda around it captures is reset, not the pointer.
  TCFuture<int> f_CopyReset() {
    int *pValue = &mp_Items[1];
    auto fResetCopy = [pValue]() mutable {
      auto fReset = [&pValue] { pValue = nullptr; };
      fReset();
    };
    fResetCopy();
    co_await fg_Work();
    co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pValue' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
};
}

namespace NRound160 {
TCFuture<int> fg_Suspend();
// An atomic removal flag, loaded.
struct CAtomicFlag {
  bool f_Load() const noexcept;
  bool f_Exchange(bool) noexcept;
};
struct CEntry {
  int m_Value;
  NMib::NStorage::TCSharedPointer<CAtomicFlag> m_pDeleted;
  ~CEntry() { m_pDeleted->f_Exchange(true); }
};
struct CItems {
  CEntry *f_FindEqual(int);
};
struct CApp : CActor {
  CItems mp_Items;
  TCFuture<int> f_Atomic() {
    auto pEntry = mp_Items.f_FindEqual(0);
    auto pDeleted = pEntry->m_pDeleted;
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      if (pDeleted->f_Load())
        return fg_MakeError("Removed");
      return {};
    });
    co_await fg_Suspend();
    co_return pEntry->m_Value;
  }
};
}

namespace NRound161 {
TCFuture<int> fg_Work();
struct CRead {
  int m_Copy;
  CRead(int const &_Value, int) : m_Copy(_Value) {}
};
struct CApp : CActor {
  TCMap<int, int> mp_Items;
  // Only the callback a constant condition chooses.
  TCFuture<int> f_Constant() {
    int *pValue = nullptr;
    auto OnResume = co_await (true ? fg_OnResume([&]() -> CExceptionPointer {
      pValue = mp_Items.f_FindEqual(0);
      if (!pValue)
        return fg_MakeError("Gone");
      return {};
    })
                                   : fg_OnResume([]() -> CExceptionPointer { return {}; }));
    co_await CAwaiter{};
    co_return *pValue;
  }
  // An argument that suspends itself, looked up before the next one does.
  TCFuture<int> f_Arguments() {
    CRead Result{mp_Items[co_await fg_Work()], co_await fg_Work()};
// CHECK-MESSAGES: :[[@LINE-1]]:11: warning: the data of the actor that this call uses is looked up before its arguments suspend, during which other calls of the actor can change or erase the data; look it up after the suspension [malterlib-coroutine-actor-reference]
    co_return Result.m_Copy;
  }
};
}

namespace NRound162 {
// An awaiter of a final class with many virtual bases, told once.
template <int t_Depth>
struct TCGraph : virtual TCGraph<t_Depth - 1>, virtual TCGraph<t_Depth - 2> {};
template <> struct TCGraph<0> {};
template <> struct TCGraph<1> : virtual TCGraph<0> {};
struct CReady {
  virtual bool await_ready() { return true; }
  void await_suspend(std::coroutine_handle<>) {}
  int await_resume() { return 0; }
};
struct CFinal final : TCGraph<24>, CReady {};
int fg_Id(int _Value);
int fg_Read(int &_Value, int _Other);
#define DRound162Four(d_Value) fg_Id(fg_Id(fg_Id(fg_Id(d_Value))))
#define DRound162Sixteen(d_Value) DRound162Four(DRound162Four(DRound162Four(DRound162Four(d_Value))))
CAwaiter fg_Work();
int fg_Use(int const &_Value, int _Other);
struct CApp : CActor {
  TCMap<int, int> mp_Items;
  TCFuture<int> f_Ready(CFinal &_Awaiter) {
    int &Value = mp_Items[0];
    fg_Read(Value, DRound162Sixteen(DRound162Sixteen(co_await _Awaiter)));
    co_return 0;
  }
  // An argument that suspends itself, looked up before another does.
  TCFuture<int> f_Arguments() { co_return fg_Use(mp_Items[co_await fg_Work()], co_await fg_Work()); }
// CHECK-MESSAGES: :[[@LINE-1]]:43: warning: the data of the actor that this call uses is looked up before its arguments suspend, during which other calls of the actor can change or erase the data; look it up after the suspension [malterlib-coroutine-actor-reference]
};
}

namespace NRound164 {
TCFuture<int> fg_Work();
int g_Value;
struct CApp : CActor {
  DMibActorEntries() TCMap<int, int> mp_Items;
  // Through a reference bound to a conditional of references bound to
  // conditionals of pointers.
  TCFuture<int> f_NestedConditionalAlias(bool _bFirst, bool _bSecond) {
    int *pFirst = &g_Value, *pSecond = &g_Value;
    int *&pAlias = _bFirst ? pFirst : pSecond;
    int *&pOuter = _bSecond ? pAlias : pSecond;
    pOuter = &mp_Items[0];
    co_await fg_Work();
    co_return *pFirst;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pFirst' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
};
struct CPhased : CPhaseActor {
  DMibActorEntries(Destroy) TCMap<int, int> mp_Items;
  // Through several unchanged aliases, of which the guard covers the phase.
  TCFuture<int> f_Aliases() {
    auto Guard = co_await f_CheckDestroyedOnResume();
    auto &Items = mp_Items;
    auto *pItems = &Items;
    auto &Alias = *pItems;
    auto &Entry = Alias[1];
    co_await fg_Work();
    co_return Entry;
  }
};
// A chain of default arguments of conditionals, with a temporary destroyed at
// the end, isn't analyzed through each of its exponentially many paths: past a
// limit, the destructors of the rest are left out, which only end their
// temporaries, and the function is analyzed.
extern bool const g_bChoose;
struct CDestroyed {
  ~CDestroyed() {}
};
int fg_Link0(CDestroyed const & = CDestroyed{}) { return 0; }
#define DLink(d_Next, d_Previous)                                              \
  int fg_Link##d_Next(int _Value = g_bChoose ? fg_Link##d_Previous()           \
                                             : fg_Link##d_Previous()) {        \
    return _Value;                                                             \
  }
DLink(1, 0) DLink(2, 1) DLink(3, 2) DLink(4, 3) DLink(5, 4) DLink(6, 5)
DLink(7, 6) DLink(8, 7) DLink(9, 8) DLink(10, 9) DLink(11, 10) DLink(12, 11)
DLink(13, 12) DLink(14, 13) DLink(15, 14) DLink(16, 15) DLink(17, 16)
DLink(18, 17) DLink(19, 18) DLink(20, 19) DLink(21, 20) DLink(22, 21)
DLink(23, 22) DLink(24, 23) DLink(25, 24) DLink(26, 25) DLink(27, 26)
DLink(28, 27) DLink(29, 28) DLink(30, 29)
#undef DLink
struct CLinked : CActor {
  TCMap<int, int> mp_Items;
  void f_Clear() { mp_Items.f_Clear(); }
  TCFuture<int> f_Run() {
    auto &Entry = mp_Items[0];
    fg_Link30();
    co_await fg_Work();
    co_return Entry;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'Entry' refers into the data of the actor
  }
};
}

namespace NRound165 {
TCFuture<int> fg_Work();
// Not an error result that a jump can skip setting.
TCAsyncResult<int> fg_MaybeError(bool _bSkip) {
  TCAsyncResult<int> Result;
  if (_bSkip)
    goto Done;
  Result.f_SetException(fg_MakeError("Stopping"));
Done:
  return Result;
}
struct CApp : CActor {
  DMibActorEntries(StopApp) TCMap<int, int> mp_Items;
  DMibActorPhaseFlag(StopApp) bool mp_bStopping = false;
  bool mp_bSkip = true;
  DMibActorPhaseEntry(StopApp) void f_Stop() {
    mp_bStopping = true;
    mp_Items.f_Clear();
  }
  TCFuture<int> f_Run() {
    auto Guard = co_await fg_OnResume([this]() -> TCAsyncResult<int> {
      if (mp_bStopping)
        return fg_MaybeError(mp_bSkip);
      return {};
    });
    int &Value = mp_Items[0];
    co_await fg_Work();
    co_return Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'Value' refers into the data of the actor and is used after a suspension; 'mp_Items' loses entries only in StopApp, so a guard of that phase that is alive at the use, like fp_CheckStoppedOrDestroyedOnResume, makes it safe [malterlib-coroutine-actor-reference]
  }
};
}

namespace NRound167 {
// A callback that looks the entry up again, but moves the entries after,
// doesn't make it valid.
struct CVector {
  int &operator[](int _Index);
  void reserve(int _Capacity);
  int capacity() const;
};
struct CWorker : CActor {
  CVector m_Items;
  TCFuture<int> f_Run() {
    int *pValue = &m_Items[0];
    auto Guard = co_await fg_OnResume([&]() -> CExceptionPointer {
      pValue = &m_Items[0];
      m_Items.reserve(m_Items.capacity() + 1);
      return {};
    });
    co_await CAwaiter{};
    co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pValue' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
};
}

namespace NRound168 {
TCFuture<int> fg_Work();
struct CApp : CActor {
  NMib::NContainer::TCMap<int, int> mp_Items;
  // Through a copy of a local pointer to the pointer.
  TCFuture<int> f_Copied() {
    int *pValue = mp_Items.f_FindEqual(1);
    if (!pValue)
      co_return 0;
    int **ppValue = &pValue;
    int **ppCopy = ppValue;
    co_await fg_Work();
    co_return **ppCopy;
// CHECK-MESSAGES: :[[@LINE-1]]:17: warning: 'pValue' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  // Not the address of the local, also taken with `addressof`, read after
  // the local is looked up again.
  TCFuture<int> f_AddressOf() {
    int *pValue = mp_Items.f_FindEqual(1);
    int **ppValue = __builtin_addressof(pValue);
    co_await fg_Work();
    pValue = mp_Items.f_FindEqual(1);
    co_return pValue ? **ppValue : 0;
  }
};
}

namespace NRound169 {
TCFuture<int> fg_Work();
struct CApp : CActor {
  DMibActorEntries(Owner) TCMap<CStr, int> mp_Items;
  // A use that the full expression of the removal evaluates after it, the
  // left side of an assignment.
  TCFuture<int> f_Assigned() {
    auto Key = fg_RandomID(mp_Items);
    auto &Entry = mp_Items[Key];
    co_await CAwaiter{};
    Entry = (mp_Items.f_Remove(Key), 0);
// CHECK-MESSAGES: :[[@LINE-1]]:5: warning: 'Entry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
    co_return 0;
  }
  // Not a removal that never runs.
  TCFuture<int> f_NeverTaken() {
    auto Key = fg_RandomID(mp_Items);
    auto &Entry = mp_Items[Key];
    if (false)
      mp_Items.f_Remove(Key);
    co_await CAwaiter{};
    co_return Entry;
  }
  // Not of another object: a reference to the object itself.
  TCFuture<int> f_Self() {
    auto &App = *this;
    auto Key = fg_RandomID(App.mp_Items);
    auto &Entry = mp_Items[Key];
    co_await fg_Work();
    int Value = Entry;
    mp_Items.f_Remove(Key);
    co_return Value;
  }
};
// Validated through many copies of the comparison.
struct CCopies : CActor {
  TCMap<int, int> mp_Items;
  TCFuture<int> f_Read() {
    int *pValue = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      bool bChanged0 = pValue != mp_Items.f_FindEqual(0);
      bool bChanged1 = bChanged0;
      bool bChanged2 = bChanged1;
      bool bChanged3 = bChanged2;
      bool bChanged4 = bChanged3;
      if (bChanged4)
        return fg_MakeError("Removed");
      return {};
    });
    co_await fg_Work();
    co_return *pValue;
  }
};
}

namespace NRound170 {
CAwaiter fg_Suspend();
// A copy of a pointer stepped from a lookup.
struct CStepped : CActor {
  TCMap<int, int> mp_Items;
  TCFuture<int> f_Stale() {
    int *pValue = &mp_Items[1];
    int *pNext = pValue++;
    co_await fg_Suspend();
    co_return *pNext;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pNext' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
};
// What removes entries, a const member function of a class with mutable
// members or a lambda called, after the entry is validated.
struct CEntry {
  TCSharedPointer<bool> m_pDeleted{new bool(false)};
  int m_Value = 42;
  ~CEntry() { *m_pDeleted = true; }
};
struct CApp : CActor {
  mutable TCMap<int, CEntry> mp_Items;
  void f_Clear() const { mp_Items.f_Clear(); }
  TCFuture<int> f_Const() {
    auto &Entry = mp_Items[0];
    auto pDeleted = Entry.m_pDeleted;
    co_await CAwaiter{};
    if (*pDeleted)
      co_return 0;
    f_Clear();
    co_return Entry.m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'Entry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_Lambda() {
    auto &Entry = mp_Items[0];
    auto pDeleted = Entry.m_pDeleted;
    co_await CAwaiter{};
    if (*pDeleted)
      co_return 0;
    auto fClear = [this]() mutable { mp_Items.f_Clear(); };
    fClear();
    co_return Entry.m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'Entry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
};
}

namespace NRound172 {
TCFuture<int> fg_Pause();
// A pointer read from an entry through a pointer to the member, or a cast,
// is of an object of its own, which the guard doesn't keep.
struct CEntry {
  int *m_pValue;
};
struct CApp : CPhaseActor {
  DMibActorEntries(Destroy) TCMap<int, CEntry> mp_Items;
  TCFuture<int> f_MemberPointer() {
    auto Guard = co_await f_CheckDestroyedOnResume();
    int *CEntry::*pmValue = &CEntry::m_pValue;
    int *pValue = mp_Items[0].*pmValue;
    co_await fg_Pause();
    co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pValue' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_Cast() {
    auto Guard = co_await f_CheckDestroyedOnResume();
    int *pValue = static_cast<int *&>(mp_Items[0].m_pValue);
    co_await fg_Pause();
    co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pValue' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
};
// What a setter gives: maybe, after it can return, through a cast, and the
// last it surely gives.
void fg_SetMaybe(int *&o_pValue, int *_pValue, bool _bSkip) {
  if (_bSkip)
    return;
  o_pValue = _pValue;
}
void fg_Set(int *&o_pValue, int *_pValue) { o_pValue = _pValue; }
int g_Value;
void fg_Select(int *&o_pValue, int *_pFirst, int *_pSecond) {
  o_pValue = _pFirst;
  o_pValue = _pSecond;
}
struct CSetter : CActor {
  TCMap<int, int> mp_Items;
  TCFuture<int> f_Maybe(bool _bSkip) {
    int *pValue = mp_Items.f_FindEqual(1);
    co_await CAwaiter{};
    fg_SetMaybe(pValue, mp_Items.f_FindEqual(1), _bSkip);
    co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pValue' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_Cast() {
    int *pValue = nullptr;
    fg_Set(static_cast<int *&>(pValue), mp_Items.f_FindEqual(1));
    co_await CAwaiter{};
    co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pValue' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_Last() {
    int *pValue = nullptr;
    fg_Select(pValue, mp_Items.f_FindEqual(1), &g_Value);
    co_await CAwaiter{};
    co_return *pValue;
  }
};
}

namespace NRound173 {
// In a lambda, a const member function of a class with mutable members that
// removes, after a removal flag is tested.
struct CEntry {
  TCSharedPointer<bool> m_pDeleted{new bool(false)};
  int m_Value = 42;
  ~CEntry() { *m_pDeleted = true; }
};
struct CApp : CActor {
  mutable TCMap<int, CEntry> mp_Items;
  void f_Clear() const { mp_Items.f_Clear(); }
  TCFuture<int> f_Test() {
    CEntry *pEntry = &mp_Items[0];
    auto pDeleted = pEntry->m_pDeleted;
    auto fRead = [this, pEntry, pDeleted] {
// CHECK-MESSAGES: :[[@LINE-1]]:25: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
      if (*pDeleted)
        return 0;
      f_Clear();
      return pEntry->m_Value;
    };
    co_await CAwaiter{};
    co_return fRead();
  }
};
}

namespace NRound175 {
TCFuture<int> fg_Work(int _Value);
// Removed by a helper through a local reference to what it is given.
void fg_Clear(TCMap<int, int> &_Items) {
  auto &Items = _Items;
  Items.f_Clear();
}
struct CApp : CActor {
  TCMap<int, int> mp_Items;
  TCFuture<int> f_Alias() {
    int *pValue = &mp_Items[0];
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pValue = &mp_Items[0];
      fg_Clear(mp_Items);
      return {};
    });
    co_await fg_Work(0);
    co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pValue' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  // Removed by a scope exit of the callback, which runs before it returns.
  TCFuture<int> f_ScopeExit() {
    int *pValue = &mp_Items[0];
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      auto Exit = g_OnScopeExit / [this] { mp_Items.f_Clear(); };
      pValue = &mp_Items[0];
      return {};
    });
    co_await fg_Work(0);
    co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pValue' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  // Not where a constant condition never runs it.
  TCFuture<int> f_Never() {
    int *pValue = &mp_Items[0];
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pValue = &mp_Items[0];
      if (false)
        mp_Items.f_Clear();
      return {};
    });
    co_await fg_Work(0);
    co_return *pValue;
  }
};
}

namespace NRound176 {
TCFuture<int> fg_Work();
void fg_Read(int const *_pValue, int _Other);
// Read before a suspension in the other arguments, which a callback that
// compares it with a fresh lookup protects.
struct CApp : CActor {
  TCMap<int, int> mp_Items;
  TCFuture<int> f_Compared() {
    int *pValue = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&, this]() -> CExceptionPointer {
      if (pValue != mp_Items.f_FindEqual(0))
        return fg_MakeError("Removed");
      return {};
    });
    fg_Read(pValue, co_await fg_Work());
    co_return 0;
  }
  // But not one that looks it up again, which leaves what was read before
  // as it was.
  TCFuture<int> f_Assigned() {
    int *pValue = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&, this]() -> CExceptionPointer {
      pValue = mp_Items.f_FindEqual(0);
      return {};
    });
    fg_Read(pValue, co_await fg_Work());
// CHECK-MESSAGES: :[[@LINE-1]]:5: warning: 'pValue' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
    co_return 0;
  }
};
}

namespace NRound179 {
struct CApp : CActor {
  TCMap<int, int> mp_Items;
};
// The object of an actor given by a reference to a pointer, captured by
// copy.
auto fg_Start(CApp *const &_pApp) {
  return fg_CallSafe([_pApp]() -> TCFuture<int> {
    int *pEntry = _pApp->mp_Items.f_FindEqual(1);
    co_await CAwaiter{};
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  });
}
// Making another object whose constructor sets a flag of its own doesn't
// start a phase of this actor.
TCFuture<int> fg_Work();
struct COtherState {
  DMibActorPhaseFlag(StopApp) bool m_bStopping = false;
  COtherState() { m_bStopping = true; }
};
struct CDistributed : CDistributedAppActor {
  DMibActorEntries(StopApp) TCMap<int, int> mp_Items;
  TCFuture<int> f_Run() {
    auto Guard = co_await fg_OnResume([this]() -> CExceptionPointer {
      COtherState Other;
      if (mp_State.m_bStoppingApp)
        return fg_MakeError("Stopping");
      return {};
    });
    int &Value = mp_Items[0];
    co_await fg_Work();
    co_return Value;
  }
};
}

namespace NRound180 {
void fg_Set(int *&o_pValue, int &_Value) { o_pValue = &_Value; }
int g_Value;
struct CApp : CActor {
  TCMap<int, int> mp_Items;
  // Set by a helper to the address of what it is given.
  TCFuture<int> f_Set() {
    int *pValue = nullptr;
    fg_Set(pValue, mp_Items[0]);
    co_await CAwaiter{};
    co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pValue' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  // Not a value a lambda's local is made with that it replaces before it
  // returns it.
  TCFuture<int> f_Replaced() {
    auto *pValue = [this] {
      auto *pInner = &mp_Items[0];
      pInner = &g_Value;
      return pInner;
    }();
    co_await CAwaiter{};
    co_return *pValue;
  }
};
}

namespace NRound181 {
// A copied removal flag compared with a number.
struct CApp : CActor {
  TCMap<CStr, CRound32Entry> mp_Flagged;
  TCFuture<int> f_NotZero() {
    auto *pEntry = mp_Flagged.f_FindEqual("Key");
    auto pDeleted = pEntry->m_pDeleted;
    co_await fg_Suspend();
    if (*pDeleted != 0)
      co_return 0;
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_One() {
    auto *pEntry = mp_Flagged.f_FindEqual("Key");
    auto pDeleted = pEntry->m_pDeleted;
    co_await fg_Suspend();
    if (*pDeleted == 1)
      co_return 0;
    co_return pEntry->m_Value;
  }
};
}

namespace NRound182 {
// Removed by a function that doesn't return, but throws to a catch around it.
struct CEntry {
  TCSharedPointer<bool> m_pDeleted;
  int m_Value = 0;
};
[[noreturn]] void fg_RemoveAndThrow(TCMap<int, CEntry> &_Items) {
  _Items.f_Remove(0);
  throw 1;
}
struct CApp : CActor {
  TCMap<int, CEntry> mp_Items;
  TCFuture<int> f_Thrown() {
    auto &Entry = mp_Items[0];
    auto pDeleted = Entry.m_pDeleted;
    co_await fg_Suspend();
    if (*pDeleted)
      co_return 0;
    try {
      fg_RemoveAndThrow(mp_Items);
    } catch (...) {
    }
    co_return Entry.m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'Entry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  // A lookup a member function gives through a pointer it is given.
  bool f_Find(int *&o_pValue) {
    o_pValue = mp_Lookups.f_FindEqual(0);
    return o_pValue != nullptr;
  }
  TCFuture<int> f_Found() {
    int *pValue = nullptr;
    if (!f_Find(pValue))
      co_return 0;
    co_await fg_Suspend();
    co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pValue' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCMap<int, int> mp_Lookups;
};
}

namespace NRound185 {
// A lambda run by fg_CallSafe that removes entries, after a test of a removal
// flag.
struct CEntry {
  int m_Value = 0;
  TCSharedPointer<bool> m_pDeleted;
};
struct CValue {
  int m_Value = 0;
};
struct CApp : CActor {
  TCMap<int, CEntry> mp_Items;
  TCMap<int, CValue> mp_Values;
  int mp_nReads = 0;
  TCFuture<int> f_Cleared() {
    auto *pEntry = mp_Items.f_FindEqual(1);
    auto pDeleted = pEntry->m_pDeleted;
    co_await fg_Suspend();
    if (*pDeleted)
      co_return 0;
    auto Clear = fg_CallSafe([this]() -> TCFuture<int> {
      mp_Items.f_Clear();
      co_return 0;
    });
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  // One that doesn't remove keeps the test.
  TCFuture<int> f_Read() {
    auto *pEntry = mp_Items.f_FindEqual(1);
    auto pDeleted = pEntry->m_pDeleted;
    co_await fg_Suspend();
    if (*pDeleted)
      co_return 0;
    auto Read = fg_CallSafe([this]() -> TCFuture<int> {
      co_return mp_nReads;
    });
    co_return pEntry->m_Value;
  }
  // A part of the entry a resume callback takes again through the pointer
  // kept from before isn't looked up again.
  TCFuture<int> f_Member() {
    auto *pEntry = mp_Values.f_FindEqual(1);
    auto *pValue = &pEntry->m_Value;
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pValue = &pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:17: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
      return {};
    });
    co_await fg_Suspend();
    co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pValue' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_Subscript() {
    auto *pSaved = mp_Values.f_FindEqual(1);
    auto *pEntry = pSaved;
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = &pSaved[0];
// CHECK-MESSAGES: :[[@LINE-1]]:17: warning: 'pSaved' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
      return {};
    });
    co_await fg_Suspend();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  // A fresh lookup, and a part of it, is.
  TCFuture<int> f_Fresh() {
    auto *pEntry = mp_Values.f_FindEqual(1);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Values.f_FindEqual(1);
      if (!pEntry)
        return CExceptionPointer{};
      return {};
    });
    co_await fg_Suspend();
    co_return pEntry->m_Value;
  }
};
}

namespace NRound186 {
// A helper that sets the pointer it is given to either of two it is given.
void fg_Select(int *&o_pValue, int *_pFirst, int *_pSecond, bool _bFirst) {
  o_pValue = _bFirst ? _pFirst : _pSecond;
}
struct CApp : CActor {
  TCMap<int, int> mp_Items;
  TCFuture<int> f_Read(bool _bFirst) {
    int *pValue = nullptr;
    fg_Select(pValue, mp_Items.f_FindEqual(1), mp_Items.f_FindEqual(2), _bFirst);
    co_await fg_Suspend();
    co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pValue' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_Second(bool _bFirst, int *_pOther) {
    int *pValue = nullptr;
    fg_Select(pValue, _pOther, mp_Items.f_FindEqual(2), _bFirst);
    co_await fg_Suspend();
    co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pValue' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
};
}

namespace NRound187 {
// A constructor given the data that removes entries after a test of a
// removal flag.
struct CEntry {
  TCSharedPointer<bool> m_pDeleted;
  int m_Value = 0;
};
struct CResetEntries {
  explicit CResetEntries(TCMap<int, CEntry> &_Items) { _Items.f_Clear(); }
};
struct CReadEntries {
  explicit CReadEntries(TCMap<int, CEntry> const &_Items) {}
};
struct CApp : CActor {
  TCMap<int, CEntry> mp_Items;
  TCFuture<int> f_Reset() {
    auto *pEntry = mp_Items.f_FindEqual(1);
    if (!pEntry)
      co_return 0;
    auto pDeleted = pEntry->m_pDeleted;
    co_await fg_Suspend();
    if (*pDeleted)
      co_return 0;
    CResetEntries Reset(mp_Items);
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_Read() {
    auto *pEntry = mp_Items.f_FindEqual(1);
    if (!pEntry)
      co_return 0;
    auto pDeleted = pEntry->m_pDeleted;
    co_await fg_Suspend();
    if (*pDeleted)
      co_return 0;
    CReadEntries Read(mp_Items);
    co_return pEntry->m_Value;
  }
};
// A lookup a static helper makes through the object it is given by pointer.
struct CValue {
  int m_Value;
};
struct CFinder : CActor {
  TCMap<int, CValue> mp_Items;
  CValue m_Fixed;
  static CValue *fs_Find(CFinder *_pThis, int _Key) {
    return _pThis->mp_Items.f_FindEqual(_Key);
  }
  static CValue *fs_Fixed(CFinder *_pThis) { return &_pThis->m_Fixed; }
  TCFuture<int> f_Run() {
    CValue *pEntry = fs_Find(this, 1);
    co_await fg_Suspend();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_Fixed() {
    CValue *pFixed = fs_Fixed(this);
    co_await fg_Suspend();
    co_return pFixed->m_Value;
  }
};
}

namespace NRound187Move {
// Moving a member out of the entry after a test of its removal flag takes it
// but removes no entry.
struct CFunctor {
  CFunctor();
  CFunctor(CFunctor &&_Other);
  ~CFunctor();
};
struct CEntry {
  TCSharedPointer<bool> m_pDeleted;
  CFunctor m_fCallback;
  int m_Value = 0;
};
struct CApp : CActor {
  TCMap<int, CEntry> mp_Items;
  TCFuture<int> f_Take() {
    auto *pEntry = mp_Items.f_FindEqual(1);
    if (!pEntry)
      co_return 0;
    auto pDeleted = pEntry->m_pDeleted;
    co_await fg_Suspend();
    if (*pDeleted)
      co_return 0;
    auto fCallback = static_cast<CFunctor &&>(pEntry->m_fCallback);
    co_return pEntry->m_Value;
  }
};
}

namespace NRound188 {
// A lambda called in a resume callback that may give back the pointer kept
// from before.
struct CApp : CActor {
  TCMap<int, int> mp_Items;
  bool mp_bRefresh = false;
  TCFuture<int> f_Read() {
    int *pValue = mp_Items.f_FindEqual(1);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pValue = [&] {
        if (!mp_bRefresh)
          return pValue;
        return mp_Items.f_FindEqual(1);
      }();
      return {};
    });
    co_await fg_Suspend();
    co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pValue' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
};
// A test of a removal flag that leaves a loop, or goes to its next round, and
// one of a flag read into a local just before.
struct CEntry {
  int m_Value = 0;
  TCSharedPointer<bool> m_pRemoved;
};
struct CPoller : CActor {
  TCMap<int, CEntry> mp_Items;
  TCFuture<int> f_Break() {
    CEntry &Entry = mp_Items[1];
    auto pRemoved = Entry.m_pRemoved;
    for (;;) {
      co_await fg_Suspend();
      if (*pRemoved)
        break;
      ++Entry.m_Value;
    }
    co_return 0;
  }
  TCFuture<int> f_Continue() {
    CEntry &Entry = mp_Items[1];
    auto pRemoved = Entry.m_pRemoved;
    for (int i = 0; i < 4; ++i) {
      co_await fg_Suspend();
      if (*pRemoved)
        continue;
      ++Entry.m_Value;
    }
    co_return 0;
  }
  TCFuture<int> f_Saved() {
    CEntry &Entry = mp_Items[1];
    auto pRemoved = Entry.m_pRemoved;
    co_await fg_Suspend();
    bool const bRemoved = *pRemoved;
    if (bRemoved)
      co_return 0;
    co_return Entry.m_Value;
  }
  TCFuture<int> f_SavedEarly() {
    CEntry &Entry = mp_Items[1];
    auto pRemoved = Entry.m_pRemoved;
    bool const bRemoved = *pRemoved;
    co_await fg_Suspend();
    if (bRemoved)
      co_return 0;
    co_return Entry.m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'Entry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
};
}

namespace NRound189 {
// A lambda that never suspends, run by fg_CallSafe, gives a future that is
// ready.
struct CApp : CActor {
  TCMap<int, int> mp_Items;
  TCFuture<int> f_Ready() {
    int *pValue = mp_Items.f_FindEqual(1);
    co_await fg_CallSafe([Value = 1]() -> TCFuture<int> {
      co_return Value;
    });
    co_return *pValue;
  }
  TCFuture<int> f_Suspends() {
    int *pValue = mp_Items.f_FindEqual(1);
    co_await fg_CallSafe([]() -> TCFuture<int> {
      co_await fg_Suspend();
      co_return 0;
    });
    co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pValue' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
};
}

namespace NRound190 {
struct CValue {
  int m_Value;
};
// A resume callback a lambda gives back on one path, and on another one that
// doesn't look up again.
struct CApp : CActor {
  TCMap<int, CValue> mp_Items;
  TCFuture<int> f_Run(bool _bRefresh) {
    auto *pValue = mp_Items.f_FindEqual(1);
    auto fSkip = [] {
      return fg_OnResume([]() -> CExceptionPointer { return {}; });
    };
    auto fWatch = [&] {
      if (_bRefresh)
        return fg_OnResume([&]() -> CExceptionPointer {
          pValue = mp_Items.f_FindEqual(1);
          if (!pValue)
            return CExceptionPointer{};
          return {};
        });
      return fSkip();
    };
    auto OnResume = co_await fWatch();
    co_await fg_Suspend();
    co_return pValue->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pValue' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  // Assigned through a reference a helper gives back to it.
  TCFuture<int> f_Alias() {
    CValue *pValue = nullptr;
    auto &pAlias = fg_Pointer(pValue);
    pAlias = mp_Items.f_FindEqual(1);
    co_await fg_Suspend();
    co_return pValue->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pValue' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  static CValue *&fg_Pointer(CValue *&_pValue) { return _pValue; }
  // Read through in a resume callback that doesn't look it up again.
  TCFuture<int> f_Logged() {
    CValue *pValue = mp_Items.f_FindEqual(1);
    auto OnResume = co_await fg_OnResume([&pValue]() -> CExceptionPointer {
      fg_Log(pValue->m_Value);
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: 'pValue' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
      return {};
    });
    co_await fg_Suspend();
    co_return 0;
  }
  static void fg_Log(int _Value);
};
}

namespace NRound192 {
TCFuture<int> fg_Suspend();
struct CEntry {
  TCSharedPointer<bool> m_pDeleted;
  int m_Value;
};
using CItems = TCMap<int, CEntry>;
// A local given the data to change, whose destructor can free, removes what a
// test of a removal flag found where it is destroyed, made as an aggregate or
// by a constructor; one given it to look at doesn't.
struct CErase {
  CItems &m_Items;
  ~CErase() { m_Items.f_Clear(); }
};
struct CReset {
  CReset(CItems &_Items) : m_Items(_Items) {}
  ~CReset() { m_Items.f_Clear(); }
  CItems &m_Items;
};
struct CLook {
  CItems const &m_Items;
  ~CLook() { (void)m_Items; }
};
struct CApp : CActor {
  CItems mp_Items;
  TCFuture<int> f_Aggregate() {
    auto *pEntry = mp_Items.f_FindEqual(1);
    auto pDeleted = pEntry->m_pDeleted;
    {
      CErase Erase{mp_Items};
      co_await fg_Suspend();
      if (*pDeleted)
        co_return 0;
    }
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_Constructed() {
    auto *pEntry = mp_Items.f_FindEqual(1);
    auto pDeleted = pEntry->m_pDeleted;
    {
      CReset Reset(mp_Items);
      co_await fg_Suspend();
      if (*pDeleted)
        co_return 0;
    }
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_Looks() {
    auto *pEntry = mp_Items.f_FindEqual(1);
    auto pDeleted = pEntry->m_pDeleted;
    {
      CLook Look{mp_Items};
      co_await fg_Suspend();
      if (*pDeleted)
        co_return 0;
    }
    co_return pEntry->m_Value;
  }
};
}

namespace NRound194 {
// A lambda given arguments by `fg_CallSafe` that doesn't suspend completes in
// it, and awaiting its future doesn't suspend.
struct CApp : CActor {
  NMib::NContainer::TCMap<int, int> mp_Items;
  TCFuture<int> f_Read() {
    int *pValue = mp_Items.f_FindEqual(1);
    co_await fg_CallSafe([](int _Value) -> TCFuture<int> { co_return _Value; }, 1);
    co_return *pValue;
  }
};
}

namespace NRound195 {
// A helper that can throw before it assigns a pointer it is given, called in
// a `try` that catches it, may leave the pointer as it was.
void fg_MayThrow();
void fg_Set(int *&o_pValue, int *_pNew) {
  fg_MayThrow();
  o_pValue = _pNew;
}
TCFuture<int> fg_Work();
struct CApp : CActor {
  NMib::NContainer::TCMap<int, int> mp_Items;
  TCFuture<int> f_Caught() {
    int Local = 0;
    int *pValue = &mp_Items[1];
    co_await fg_Work();
    try {
      fg_Set(pValue, &Local);
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: 'pValue' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
    } catch (...) {
    }
    co_return *pValue;
  }
  TCFuture<int> f_Uncaught() {
    int Local = 0;
    int *pValue = &mp_Items[1];
    co_await fg_Work();
    fg_Set(pValue, &Local);
    co_return *pValue;
  }
};
}

namespace NRound196 {
TCFuture<int> fg_Suspend();
void fg_Clear(NMib::NContainer::TCMap<int, int> &_Items) { _Items.f_Clear(); }
void fg_Log(bool _bValue);
void fg_LogValue(int _Value);
template <typename t_CType>
struct TCAtomic {
  TCAtomic(t_CType _Value);
  t_CType f_Load() const noexcept;
};
struct CApp : CActor {
  NMib::NContainer::TCMap<int, int> mp_Items;
  DMibActorPhaseFlag(StopApp) TCAtomic<bool> mp_bStopping{false};
  DMibActorEntries(StopApp) NMib::NContainer::TCMap<int, int> mp_Stable;
  // A callback that removes entries through a local reference to the
  // collection, given to a helper or assigned.
  TCFuture<int> f_ClearAlias() {
    int *pEntry = nullptr;
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(1);
      auto &Items = mp_Items;
      fg_Clear(Items);
      return {};
    });
    co_await fg_Suspend();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_AssignAlias() {
    int *pEntry = nullptr;
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(1);
      auto &Items = mp_Items;
      Items = {};
      return {};
    });
    co_await fg_Suspend();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  // A phase flag read through Malterlib's atomics.
  TCFuture<int> f_Phase() {
    auto OnResume = co_await fg_OnResume([this]() -> CExceptionPointer {
      if (mp_bStopping.f_Load())
        return fg_MakeError("Stopped");
      return {};
    });
    int &Value = mp_Stable[1];
    co_await fg_Suspend();
    co_return Value;
  }
  // A comparison only logged doesn't re-validate, and a lambda called in
  // place reads before the lookup again.
  TCFuture<int> f_Logged() {
    int *p = &mp_Items[1];
    auto Guard = co_await fg_OnResume([&]() -> CExceptionPointer {
      fg_Log(p == mp_Items.f_FindEqual(1));
      fg_LogValue(*p);
// CHECK-MESSAGES: :[[@LINE-1]]:20: warning: 'p' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
      p = mp_Items.f_FindEqual(1);
      return p ? CExceptionPointer{} : fg_MakeError("removed");
    });
    co_await fg_Suspend();
    co_return *p;
  }
  TCFuture<int> f_Helper() {
    int *p = &mp_Items[1];
    auto Guard = co_await fg_OnResume([&]() -> CExceptionPointer {
      [&] { fg_LogValue(*p); }();
// CHECK-MESSAGES: :[[@LINE-1]]:26: warning: 'p' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
      p = mp_Items.f_FindEqual(1);
      return p ? CExceptionPointer{} : fg_MakeError("removed");
    });
    co_await fg_Suspend();
    co_return *p;
  }
  // A lookup on every branch re-validates, on one of them doesn't, and one
  // that keeps what it had on another doesn't either.
  TCFuture<int> f_Branches(int _Mode) {
    int *p = &mp_Items[1];
    auto Guard = co_await fg_OnResume([&]() -> CExceptionPointer {
      if (_Mode)
        p = mp_Items.f_FindEqual(1);
      else
        p = mp_Items.f_FindEqual(2);
      return p ? CExceptionPointer{} : fg_MakeError("removed");
    });
    co_await fg_Suspend();
    co_return *p;
  }
  TCFuture<int> f_OneBranch(int _Mode) {
    int *p = &mp_Items[1];
    auto Guard = co_await fg_OnResume([&]() -> CExceptionPointer {
      if (_Mode)
        p = mp_Items.f_FindEqual(1);
      return p ? CExceptionPointer{} : fg_MakeError("removed");
    });
    co_await fg_Suspend();
    co_return *p;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'p' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_KeptBranch(int _Mode) {
    int *p = &mp_Items[1];
    int *pSaved = p;
    auto Guard = co_await fg_OnResume([&]() -> CExceptionPointer {
      if (_Mode)
        p = mp_Items.f_FindEqual(1);
      else
        p = pSaved;
      return p ? CExceptionPointer{} : fg_MakeError("removed");
    });
    co_await fg_Suspend();
    co_return *p;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'p' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
};
}

namespace NRound197 {
TCFuture<int> fg_Work();
// A copy or a move of an iterator refers to the same entry.
struct CApp : CActor {
  NMib::NContainer::TCMap<int, int> mp_Items;
  TCFuture<int> f_Copy() {
    auto iEntry = mp_Items.f_GetIterator();
    auto iCopy = iEntry;
    co_await fg_Work();
    co_return *iCopy;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'iCopy' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_Move() {
    auto iEntry = mp_Items.f_GetIterator();
    auto iMoved = fg_Move(iEntry);
    co_await fg_Work();
    co_return *iMoved;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'iMoved' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  // A scope exit that catches what its callback throws runs it when it is
  // destroyed too.
  TCFuture<int> f_Catch() {
    auto *pEntry = mp_Items.f_FindEqual(1);
    auto Cleanup = NMib::g_OnScopeExitCatch / [pEntry] { ++*pEntry; };
// CHECK-MESSAGES: :[[@LINE-1]]:48: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
    co_await fg_Work();
    co_return 0;
  }
};
}

namespace NRound198 {
TCFuture<int> fg_Work();
struct CEntry {
  int m_Value;
};
struct ICompletion {
  int f_Use();
};
struct ISocket {
  virtual ICompletion *f_GetCompletion() { return nullptr; }
};
template <typename t_CType>
struct TCUniquePointer {
  t_CType *operator->() const;
};
struct CApp : CActor {
  NMib::NContainer::TCMap<int, int> mp_Items;
  NMib::NContainer::TCMap<int, CEntry> mp_Entries;
  NMib::NContainer::TCMap<int, TCUniquePointer<ISocket>> mp_Sockets;
  // A generic lambda that `fg_CallSafe` runs suspends where its
  // specialization does.
  TCFuture<int> f_Generic() {
    int &Entry = mp_Items[1];
    auto fWork = [](auto _Future) -> TCFuture<int> { co_return co_await fg_Move(_Future); };
    co_await fg_CallSafe(fWork, fg_Work());
    co_return Entry;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'Entry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  // What an own coroutine gives back, awaited, can be a lookup.
  TCFuture<CEntry *> fp_Find(int _Key) { co_return mp_Entries.f_FindEqual(_Key); }
  TCFuture<int> f_Awaited(int _Key) {
    auto *pEntry = co_await fp_Find(_Key);
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  // A virtual getter whose own body gives back static storage can be
  // overridden to give something else.
  TCFuture<int> f_Virtual() {
    auto *pCompletion = mp_Sockets[1]->f_GetCompletion();
    co_await fg_Work();
    co_return pCompletion->f_Use();
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pCompletion' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
};
}

namespace NRound199 {
TCFuture<int> fg_Work();
struct CApp : CActor {
  NMib::NContainer::TCMap<int, int> mp_Items;
  int m_Value;
  // A helper whose scope exit puts back what the caller had doesn't look it
  // up again; one that does on every branch does.
  void f_Restore(int *&o_pEntry) {
    int *pSaved = o_pEntry;
    auto Cleanup = NMib::g_OnScopeExit / [&] { o_pEntry = pSaved; };
    o_pEntry = mp_Items.f_FindEqual(1);
  }
  void f_RefreshBranches(int *&o_pEntry, bool _bFirst) {
    if (_bFirst)
      o_pEntry = mp_Items.f_FindEqual(1);
    else
      o_pEntry = mp_Items.f_FindEqual(2);
  }
  TCFuture<int> f_Restored() {
    int *pEntry = mp_Items.f_FindEqual(1);
    auto OnResume = co_await fg_OnResume([&] {
      f_Restore(pEntry);
      if (!pEntry)
        return fg_MakeError("Entry removed");
      return CExceptionPointer{};
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_Branches(bool _bFirst) {
    int *pEntry = mp_Items.f_FindEqual(1);
    auto OnResume = co_await fg_OnResume([&] {
      f_RefreshBranches(pEntry, _bFirst);
      if (!pEntry)
        return fg_MakeError("Entry removed");
      return CExceptionPointer{};
    });
    co_await fg_Work();
    co_return *pEntry;
  }
  // What an awaited coroutine lambda gives back, and only what a constant
  // condition leaves in an awaited coroutine.
  template <bool t_bCached>
  TCFuture<int *> f_Find() {
    if (t_bCached)
      co_return mp_Items.f_FindEqual(1);
    else
      co_return &m_Value;
  }
  TCFuture<int> f_Lambda() {
    auto fFind = [this]() -> TCFuture<int *> { co_return mp_Items.f_FindEqual(1); };
    auto *pValue = co_await fFind();
    co_await fg_Work();
    co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pValue' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_Constant() {
    auto *pValue = co_await f_Find<false>();
    co_await fg_Work();
    co_return *pValue;
  }
  TCFuture<int> f_ConstantCached() {
    auto *pValue = co_await f_Find<true>();
    co_await fg_Work();
    co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pValue' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
};
}

namespace NRound201 {
TCFuture<int> fg_Suspend();
struct CEntry {
  int m_Value;
};
struct CApp : CActor {
  TCMap<int, CEntry> mp_Items;
  TCMap<int, int> mp_Values;
  // An entry a lambda held in a local looks up in what it is given, also a
  // generic one, but not what it gives of its own.
  TCFuture<int> f_Lambda() {
    auto fFind = [](TCMap<int, CEntry> &_Items) {
      return _Items.f_FindEqual(1);
    };
    CEntry *pEntry = fFind(mp_Items);
    co_await fg_Suspend();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_GenericLambda() {
    auto fFind = [](auto &_Items) { return _Items.f_FindEqual(1); };
    CEntry *pEntry = fFind(mp_Items);
    co_await fg_Suspend();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_OwnLambda() {
    static CEntry s_Entry;
    auto fFind = [](TCMap<int, CEntry> &_Items) { return &s_Entry; };
    CEntry *pEntry = fFind(mp_Items);
    co_await fg_Suspend();
    co_return pEntry->m_Value;
  }
  // What a coroutine of the actor gives back, awaited through a local that
  // holds its future, moved, and moved as it is made.
  TCFuture<int *> f_Find() { co_return mp_Values.f_FindEqual(1); }
  TCFuture<int> f_Saved() {
    auto Pending = f_Find();
    int *pValue = co_await fg_Move(Pending);
    co_await fg_Suspend();
    co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pValue' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_Moved() {
    int *pValue = co_await fg_Move(f_Find());
    co_await fg_Suspend();
    co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pValue' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
};
}

namespace NRound202 {
TCFuture<int> fg_Work();
struct CValue {
  NMib::NStr::CStr m_Name;
};
// A field of an entry that a helper of the actor is given by reference and
// gives back.
struct CApp : CActor {
  TCMap<int, CValue> mp_Items;
  TCMap<int, int> mp_Values;
  NMib::NStr::CStr m_Name;
  NMib::NStr::CStr const &f_Name(CValue const &_Value) { return _Value.m_Name; }
  NMib::NStr::CStr const *f_NameAddress(CValue const &_Value) {
    return &_Value.m_Name;
  }
  TCFuture<NMib::NStr::CStr> f_Field() {
    NMib::NStr::CStr const &Name = f_Name(mp_Items[1]);
    co_await fg_Work();
    co_return Name;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'Name' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<NMib::NStr::CStr> f_FieldAddress() {
    NMib::NStr::CStr const *pName = f_NameAddress(mp_Items[1]);
    co_await fg_Work();
    co_return *pName;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pName' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  // A virtual getter called by name runs the body that is seen.
  virtual NMib::NStr::CStr const &f_OwnName() const { return m_Name; }
  TCFuture<NMib::NStr::CStr> f_Qualified() {
    NMib::NStr::CStr const &Name = CApp::f_OwnName();
    co_await fg_Work();
    co_return Name;
  }
};
// What `fg_CallSafe` gives back of what it calls.
template <typename tf_FFunctor>
auto fg_CallSafe(tf_FFunctor &&_fFunctor) -> decltype(_fFunctor());
struct CCaller : CActor {
  TCMap<int, int> mp_Values;
  TCFuture<int> f_Run() {
    int *pValue = co_await fg_CallSafe([this]() -> TCFuture<int *> {
      co_return mp_Values.f_FindEqual(1);
    });
    co_await fg_Work();
    co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pValue' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
};
}

namespace NRound203 {
TCFuture<int> fg_Work();
struct CValue {
  int m_Value;
};
template <typename tf_FFunctor>
auto fg_CallSafe(tf_FFunctor &&_fFunctor) -> decltype(_fFunctor());
struct CApp : CActor {
  TCMap<int, CValue> mp_Items;
  TCMap<int, int> mp_Values;
  // An iterator assigned a fresh one only on some paths, or a copy of a saved
  // one, isn't looked up again.
  TCFuture<int> f_Conditional(bool _bRefresh) {
    auto iEntry = mp_Items.f_GetIterator(1);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      if (_bRefresh)
        iEntry = mp_Items.f_GetIterator(1);
      return {};
    });
    co_await fg_Work();
    co_return iEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'iEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_Saved() {
    auto iEntry = mp_Items.f_GetIterator(1);
    auto iSaved = iEntry;
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      iEntry = iSaved;
      return {};
    });
    co_await fg_Work();
    co_return iEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'iEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_Fresh() {
    auto iEntry = mp_Items.f_GetIterator(1);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      iEntry = mp_Items.f_GetIterator(1);
      return {};
    });
    co_await fg_Work();
    co_return iEntry->m_Value;
  }
  // What a local coroutine lambda given to `fg_CallSafe` gives back, also
  // moved.
  TCFuture<int> f_LocalSafe() {
    auto fFind = [this]() -> TCFuture<int *> {
      co_return mp_Values.f_FindEqual(1);
    };
    auto pValue = co_await fg_CallSafe(fg_Move(fFind));
    co_await fg_Work();
    co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pValue' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  // A reference bound to the pointer in the callback reads nothing.
  TCFuture<int> f_Alias() {
    auto pValue = mp_Values.f_FindEqual(1);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      int *&pAlias = pValue;
      pAlias = mp_Values.f_FindEqual(1);
      if (!pAlias)
        return fg_MakeError("Entry removed");
      return {};
    });
    co_await fg_Work();
    co_return *pValue;
  }
};
}

namespace NRound204 {
TCFuture<int> fg_Suspend();
CExceptionPointer fg_CheckError();
struct CValue {
  int m_Value;
};
struct CApp : CActor {
  TCMap<int, CValue> mp_Items;
  TCMap<int, int> mp_Values;
  // A jump past the lookup again leaves it stale on that path.
  TCFuture<int> f_Jump(bool _bSkip) {
    auto *pValue = mp_Items.f_FindEqual(1);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      if (_bSkip)
        goto Done;
      pValue = mp_Items.f_FindEqual(1);
    Done:
      return {};
    });
    co_await fg_Suspend();
    co_return pValue->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pValue' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  // A helper that checks for an error can give none.
  TCFuture<int> f_Check(bool _bCheck) {
    auto *pValue = &mp_Values[1];
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      if (_bCheck)
        return fg_CheckError();
      pValue = mp_Values.f_FindEqual(1);
      return {};
    });
    co_await fg_Suspend();
    co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pValue' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  // Only the branch that goes on looks it up again; the other leaves with an
  // error.
  TCFuture<int> f_ErrorBranch(bool _bCheck) {
    auto *pValue = &mp_Values[1];
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      if (_bCheck)
        pValue = mp_Values.f_FindEqual(1);
      else
        return fg_MakeError("Stop");
      if (!pValue)
        return fg_MakeError("Removed");
      return {};
    });
    co_await fg_Suspend();
    co_return *pValue;
  }
};
}

namespace NRound206 {
TCFuture<int> fg_Suspend();
struct CValue {
  int m_Value;
};
template <typename t_CType, unsigned t_nDim>
struct TCVec {
  t_CType m_Axis[t_nDim];
  t_CType &operator[](unsigned _iAxis) { return m_Axis[_iAxis]; }
};
struct CApp : CActor {
  TCMap<int, int> mp_Items;
  TCMap<int, CValue> mp_Values;
  TCVec<int, 2> mp_Position;
  // What `fg_Exchange` gives back is what it held, also where it is
  // assigned a fresh lookup.
  TCFuture<int> f_Exchange() {
    int *pEntry = mp_Items.f_FindEqual(0);
    co_await fg_Suspend();
    co_return *fg_Exchange(pEntry, mp_Items.f_FindEqual(0));
// CHECK-MESSAGES: :[[@LINE-1]]:28: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  // Through a pointer to a local iterator.
  TCFuture<int> f_Pointer() {
    auto iEntry = mp_Values.f_GetIterator();
    auto *piEntry = &iEntry;
    co_await fg_Suspend();
    co_return (*piEntry)->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:17: warning: 'iEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  // An element of a fixed array the object holds lives with it.
  TCFuture<int> f_Fixed() {
    int &X = mp_Position[0];
    co_await fg_Suspend();
    co_return X;
  }
};
// A guard that the kept branch of an `if constexpr` leaves with an error, of
// the specialization that keeps it.
struct CPhaseApp : CPhaseActor {
  DMibActorEntries(Destroy) TCMap<int, int> mp_Items;
  template <bool t_bCheckDestroy>
  TCFuture<int> f_Test() {
    auto Guard = co_await fg_OnResume([this]() -> CExceptionPointer {
      if (f_IsDestroyed()) {
        if constexpr (t_bCheckDestroy)
          return fg_MakeError("Destroyed");
      }
      return {};
    });
    int &Entry = mp_Items[0];
    co_await fg_Suspend();
    co_return Entry;
  }
};
void fg_Test(CPhaseApp &_App) { _App.f_Test<true>(); }
}

namespace NRound207 {
TCFuture<int> fg_Work();
void fg_Log(int _Value);
struct CApp : CActor {
  TCMap<int, int> mp_Items;
  // A read in one branch before the look up again of that branch, whatever
  // another branch does first.
  TCFuture<int> f_Branch(bool _bFirst) {
    int *pEntry = &mp_Items[0];
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      if (_bFirst) {
        pEntry = mp_Items.f_FindEqual(0);
      } else {
        fg_Log(*pEntry);
// CHECK-MESSAGES: :[[@LINE-1]]:17: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
        pEntry = mp_Items.f_FindEqual(0);
      }
      if (!pEntry)
        return fg_MakeError("gone");
      return {};
    });
    co_await fg_Work();
    co_return *pEntry;
  }
  // Each case of a switch with a default looks it up again.
  TCFuture<int> f_Switch(int _Mode) {
    int *pEntry = nullptr;
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      switch (_Mode) {
      case 0:
        pEntry = mp_Items.f_FindEqual(0);
        break;
      default:
        pEntry = mp_Items.f_FindEqual(1);
        break;
      }
      if (!pEntry)
        return fg_MakeError("gone");
      return {};
    });
    co_await fg_Work();
    co_return *pEntry;
  }
};
}

namespace NRound218 {
TCFuture<int> fg_Work();
struct CApp : CActor {
  TCMap<int, int> mp_Items;
  // The callback runs when it is registered, after a suspension that can
  // have removed what it reads before it looks it up again.
  TCFuture<int> f_RegisteredLate(int _Key) {
    int *pValue = mp_Items.f_FindEqual(_Key);
    co_await fg_Work();
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      if (*pValue)
// CHECK-MESSAGES: :[[@LINE-1]]:12: warning: 'pValue' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
        return fg_MakeError("Value");
      return {};
    });
    co_return 0;
  }
  // Not before a suspension.
  TCFuture<int> f_RegisteredEarly(int _Key) {
    int *pValue = mp_Items.f_FindEqual(_Key);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      if (*pValue)
        return fg_MakeError("Value");
      return {};
    });
    co_return 0;
  }
  // Nor what it looks up again first.
  TCFuture<int> f_LookedUpFirst(int _Key) {
    int *pValue = mp_Items.f_FindEqual(_Key);
    co_await fg_Work();
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pValue = mp_Items.f_FindEqual(_Key);
      if (!pValue || *pValue)
        return fg_MakeError("Value");
      return {};
    });
    co_return 0;
  }
};
}

namespace NRound219 {
TCFuture<int> fg_Work();
struct CApp : CActor {
  TCMap<int, int> mp_Items;
  // A case that can break before it looks the entry up again.
  TCFuture<int> f_Skipped(int _Mode, bool _bSkip) {
    int *pValue = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      switch (_Mode) {
      case 0:
        if (_bSkip)
          break;
        pValue = mp_Items.f_FindEqual(0);
        break;
      default:
        pValue = mp_Items.f_FindEqual(1);
        break;
      }
      return {};
    });
    co_await fg_Work();
    co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pValue' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  // A local the callback looks up itself, assigned on every path that
  // succeeds.
  TCFuture<int> f_FoundLocally(int _Key) {
    int *pValue = mp_Items.f_FindEqual(_Key);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      if (auto *pFound = mp_Items.f_FindEqual(_Key); !pFound)
        return fg_MakeError("Removed");
      else
        pValue = pFound;
      return {};
    });
    co_await fg_Work();
    co_return *pValue;
  }
  // Not one kept from before the suspension.
  TCFuture<int> f_KeptFromBefore(int _Key) {
    int *pValue = mp_Items.f_FindEqual(_Key);
    int *pSaved = pValue;
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      if (!pSaved)
        return fg_MakeError("Removed");
      else
        pValue = pSaved;
      return {};
    });
    co_await fg_Work();
    co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pValue' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
};
}

namespace NRound220 {
TCFuture<int> fg_Work();
struct CValue {
  int f_Read(int _Value) const;
};
struct CApp : CActor {
  DMibActorEntries(StopApp) TCMap<int, CValue> mp_Items;
  DMibActorPhaseFlag(StopApp) bool mp_bStoppingApp = false;
  DMibActorPhaseGuard(StopApp) TCFuture<COnResumeScope> f_CheckStoppedOnResume() const;
  DMibActorPhaseEntry(StopApp) int f_ApplyStopResult(int _Result) {
    mp_bStoppingApp = true;
    mp_Items.f_Clear();
    return _Result;
  }
  int f_Plain(int _Result);
  // An argument that starts the phase after the suspension, which the guard
  // checked before, removes the entry looked up before it.
  TCFuture<int> f_StartedInArguments() {
    auto Guard = co_await f_CheckStoppedOnResume();
    co_return mp_Items[1].f_Read(f_ApplyStopResult(co_await fg_Work()));
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: the data of the actor that this call uses is looked up before its arguments suspend, during which other calls of the actor can change or erase the data; look it up after the suspension [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_Guarded() {
    auto Guard = co_await f_CheckStoppedOnResume();
    co_return mp_Items[1].f_Read(f_Plain(co_await fg_Work()));
  }
};
// A coroutine called directly that never suspends gives a ready future.
TCFuture<int> fg_Ready() { co_return 0; }
struct CReady : CActor {
  TCMap<int, int> mp_Items;
  TCFuture<int> f_Read() {
    int &Value = mp_Items[1];
    co_await fg_Ready();
    co_return Value;
  }
  TCFuture<int> f_ReadAfterWork() {
    int &Value = mp_Items[1];
    co_await fg_Work();
    co_return Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'Value' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
};
}

namespace NRound221 {
TCFuture<int> fg_Work();
struct CValue {
  int m_Value;
};
struct CApp : CActor {
  TCMap<int, CValue> mp_Items;
  // An iterator a lambda copies is used where the lambda runs.
  TCFuture<int> f_CopiedIterator() {
    auto iEntry = mp_Items.f_GetIterator();
    auto fRead = [iEntry] { return iEntry->m_Value; };
// CHECK-MESSAGES: :[[@LINE-1]]:19: warning: 'iEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
    co_await fg_Work();
    co_return fRead();
  }
  TCFuture<int> f_CopiedIteratorBefore() {
    auto iEntry = mp_Items.f_GetIterator();
    auto fRead = [iEntry] { return iEntry->m_Value; };
    int Value = fRead();
    co_await fg_Work();
    co_return Value;
  }
  // A result set before a return that leaves with it ends the coroutine,
  // whatever is done to it after that return.
  TCFuture<int> f_ClearedAfter() {
    auto *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> TCAsyncResult<int> {
      TCAsyncResult<int> Result;
      Result.f_SetResult(0);
      if (pEntry != mp_Items.f_FindEqual(0))
        return Result;
      Result.f_Clear();
      return Result;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  // Not one cleared before it.
  TCFuture<int> f_ClearedBefore() {
    auto *pEntry = mp_Items.f_FindEqual(0);
    auto OnResume = co_await fg_OnResume([&]() -> TCAsyncResult<int> {
      TCAsyncResult<int> Result;
      Result.f_SetResult(0);
      Result.f_Clear();
      if (pEntry != mp_Items.f_FindEqual(0))
        return Result;
      return Result;
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
};
}

namespace NRound221Owned {
TCFuture<int> fg_Work();
struct CState {
  int m_Value;
};
// A copy of a shared pointer in the data owns what it points to.
struct CApp : CActor {
  TCMap<int, TCSharedPointer<CState>> mp_States;
  TCFuture<int> f_CopiedOwner() {
    auto &pState = mp_States[0];
    auto fRead = [pState] { return pState->m_Value; };
    co_await fg_Work();
    co_return fRead();
  }
};
}

namespace NRound222 {
TCFuture<int> fg_Work();
struct CApp : CActor {
  TCMap<int, int> mp_Items;
  // A getter's local that may hold a lookup, on one of its paths.
  int *f_Find(int _Key, bool _bFind) {
    int *pValue = nullptr;
    if (_bFind)
      pValue = mp_Items.f_FindEqual(_Key);
    return pValue;
  }
  TCFuture<int> f_FoundMaybe(int _Key, bool _bFind) {
    int *pValue = f_Find(_Key, _bFind);
    if (!pValue)
      co_return 0;
    co_await fg_Work();
    co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pValue' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
};
// A rollback cleared before it can run starts no phase.
struct CGuarded : CPhaseActor {
  DMibActorEntries(Destroy) TCMap<int, int> mp_Items;
  TCFuture<int> f_RolledBack() {
    auto Guard = co_await f_CheckDestroyedOnResume();
    auto *pEntry = &mp_Items[1];
    auto Resume = co_await fg_OnResume([this] {
      auto Rollback = g_OnScopeExit / [this] { fp_DestroyInternal(); };
      Rollback.f_Clear();
      return CExceptionPointer{};
    });
    co_await fg_Work();
    co_return *pEntry;
  }
  // Not one that can run first.
  TCFuture<int> f_NotRolledBack(bool _bEarly) {
    auto Guard = co_await f_CheckDestroyedOnResume();
    auto *pEntry = &mp_Items[1];
    auto Resume = co_await fg_OnResume([this, _bEarly] {
      auto Rollback = g_OnScopeExit / [this] { fp_DestroyInternal(); };
      if (_bEarly)
        return CExceptionPointer{};
      Rollback.f_Clear();
      return CExceptionPointer{};
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension; 'mp_Items' loses entries only in Destroy, so a guard of that phase that is alive at the use, like f_CheckDestroyedOnResume, makes it safe [malterlib-coroutine-actor-reference]
  }
  DMibActorEntries(Destroy) TCMap<int, int> mp_First, mp_Second;
  // Of two collections that lose entries in the same phases, which the same
  // guard keeps.
  TCFuture<int> f_Either(bool _bFirst) {
    auto Guard = co_await f_CheckDestroyedOnResume();
    auto &Value = _bFirst ? mp_First[1] : mp_Second[1];
    co_await fg_Work();
    co_return Value;
  }
};
struct CClearFunctor : CPhaseActor {
  TCMap<int, int> mp_Items;
  // Clearing the functor cancels it too.
  TCFuture<int> f_ClearedFunctor() {
    auto *pEntry = &mp_Items[1];
    auto Resume = co_await fg_OnResume([this, &pEntry] {
      auto Rollback = g_OnScopeExit / [this] { mp_Items.f_Clear(); };
      pEntry = &mp_Items[1];
      Rollback.f_ClearFunctor();
      return CExceptionPointer{};
    });
    co_await fg_Work();
    co_return *pEntry;
  }
};
}

namespace NRound225 {
TCFuture<int> fg_Work();
template <typename t_CData>
struct TCVector {
  t_CData *f_GetArray();
  t_CData &f_Insert(t_CData const &_Value);
  TCVector &operator<<(t_CData const &_Value) {
    f_Insert(_Value);
    return *this;
  }
};
struct CEntry {
  int m_Value;
};
struct CApp : CActor {
  TCVector<int> mp_Values;
  TCMap<int, CEntry> mp_Items;
  // Appending with an operator that inserts can move what was looked up
  // again.
  TCFuture<int> f_Appended() {
    int *pValue = mp_Values.f_GetArray();
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pValue = mp_Values.f_GetArray();
      mp_Values << 42;
      return {};
    });
    co_await fg_Work();
    co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pValue' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  // A read the callback never runs, before it looks the entry up again.
  TCFuture<int> f_DiscardedRead() {
    auto *pEntry = mp_Items.f_FindEqual(1);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      if constexpr (false)
        (void)pEntry->m_Value;
      pEntry = mp_Items.f_FindEqual(1);
      if (!pEntry)
        return fg_MakeError("Removed");
      return {};
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
  TCFuture<int> f_TakenRead() {
    auto *pEntry = mp_Items.f_FindEqual(1);
    co_await fg_Work();
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      if constexpr (true)
        (void)pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
      pEntry = mp_Items.f_FindEqual(1);
      if (!pEntry)
        return fg_MakeError("Removed");
      return {};
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
};
}

namespace NRound230
{
	using namespace NMib::NContainer;
	TCFuture<int> fg_Work();

	// What the callback of fg_OnResume always assigns, in the condition of an
	// `if`, is fresh, also with a declaration in its initializer.
	struct CApp : CActor
	{
		TCMap<int, int> mp_Items;

		TCFuture<int> f_Declared()
		{
			int *pValue = mp_Items.f_FindEqual(0);
			auto OnResume = co_await fg_OnResume
				(
					[&]() -> CExceptionPointer
					{
						if (auto *pFresh = mp_Items.f_FindEqual(0); pFresh != pValue)
							return fg_MakeError("Entry changed");
						return {};
					}
				)
			;
			co_await fg_Work();
			co_return *pValue;
		}

		TCFuture<int> f_Condition()
		{
			int *pValue = nullptr;
			auto OnResume = co_await fg_OnResume
				(
					[&]() -> CExceptionPointer
					{
						if (!(pValue = mp_Items.f_FindEqual(0)))
							return fg_MakeError("Entry gone");
						return {};
					}
				)
			;
			co_await fg_Work();
			co_return *pValue;
		}
	};

	// The guard a constant condition chooses guards what it guards.
	struct CDistributed : CDistributedAppActor
	{
		DMibActorEntries(Destroy, StopApp) TCMap<int, int> mp_Items;

		template <bool t_bStop>
		TCFuture<int> f_Run()
		{
			auto Guard = t_bStop ? co_await fp_CheckStoppedOrDestroyedOnResume() : co_await f_CheckDestroyedOnResume();
			auto *pValue = mp_Items.f_FindEqual(1);
			co_await fg_Work();
			co_return *pValue;
		}
	};

	void fg_Instantiate(CDistributed *_pApp)
	{
		_pApp->f_Run<true>();
	}
}

namespace NRound232
{
	// An assignment evaluates what it assigns first, so a lookup in what it
	// assigns to runs after the value suspends.
	TCFuture<int> fg_Work();

	struct CApp : CActor
	{
		TCMap<int, int> mp_Items;

		TCFuture<int> f_Run()
		{
			*mp_Items.f_FindEqual(1) = co_await fg_Work();
			co_return 0;
		}
	};
}

namespace NRound233
{
	using namespace NMib::NContainer;
	CAwaiter fg_Suspend();

	// What a function sets a pointer it is given the address of to, and a
	// guard whose callback always gives an error through a helper whose other
	// branch a constant condition never takes.
	void fg_Set(int **_ppTarget, int *_pSource)
	{
		*_ppTarget = _pSource;
	}

	template <bool t_bAllowSuccess>
	CExceptionPointer fg_StoppedError()
	{
		if (t_bAllowSuccess)
			return {};
		return fg_MakeError("Stopped");
	}

	struct CApp : CActor
	{
		DMibActorPhaseFlag(StopApp) bool m_bStopping = false;
		DMibActorEntries(StopApp) TCMap<int, int> mp_Guarded;
		TCMap<int, int> mp_Items;

		TCFuture<int> f_Read()
		{
			int *pValue = nullptr;
			fg_Set(&pValue, mp_Items.f_FindEqual(1));
			co_await fg_Suspend();
			co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pValue' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
		}

		TCFuture<int> f_Guarded()
		{
			auto OnResume = co_await fg_OnResume
				(
					[this]() -> CExceptionPointer
					{
						if (m_bStopping)
							return fg_StoppedError<false>();
						return {};
					}
				)
			;
			int *pValue = mp_Guarded.f_FindEqual(1);
			co_await fg_Suspend();
			co_return *pValue;
		}
	};
}

namespace NRound235 {
TCFuture<int> fg_Work();
struct CEntry {
  int m_Value;
};
int fg_Read(CEntry const *_pEntry) { return _pEntry->m_Value; }
struct CApp : CActor {
  TCMap<int, CEntry> mp_Items;
  TCMap<int, int> mp_Values;
  int m_Default = 0;
  // The address of what a reference refers to is of the entry.
  TCFuture<int> f_Address() {
    CEntry &Entry = mp_Items[0];
    co_await fg_Work();
    co_return fg_Read(&Entry);
// CHECK-MESSAGES: :[[@LINE-1]]:24: warning: 'Entry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  // Not what a getter returns after it always returned.
  template <bool t_bDefault>
  int *f_Find(int _Key) {
    if constexpr (t_bDefault)
      return &m_Default;
    return mp_Values.f_FindEqual(_Key);
  }
  TCFuture<int> f_Default() {
    int *pValue = f_Find<true>(0);
    co_await fg_Work();
    co_return *pValue;
  }
  TCFuture<int> f_Found() {
    int *pValue = f_Find<false>(0);
    co_await fg_Work();
    co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pValue' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
};
}

namespace NRound236 {
TCFuture<int> fg_Work();
struct CApp : CActor {
  DMibActorPhaseFlag(Destroy) bool f_IsShuttingDown() const;
  DMibActorPhaseFlag(Destroy) bool m_bShuttingDown = false;
  DMibActorEntries(Destroy) TCMap<int, int> mp_Items;
  // A reference to a copy of the flag holds what it was.
  TCFuture<int> f_Snapshot() {
    auto const &bShuttingDown = f_IsShuttingDown();
    auto Resume = co_await fg_OnResume([&]() -> CExceptionPointer {
      if (bShuttingDown)
        return fg_MakeError("Shutting down");
      return {};
    });
    int &Value = mp_Items[1];
    co_await fg_Work();
    co_return Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'Value' refers into the data of the actor and is used after a suspension; 'mp_Items' loses entries only in Destroy, so a guard of that phase that is alive at the use, like f_CheckDestroyedOnResume, makes it safe [malterlib-coroutine-actor-reference]
  }
  // So does one to what is made of it, `b0 || b0`, also through many made of
  // others, each told once.
  TCFuture<int> f_Chain() {
    bool const &b0 = m_bShuttingDown;
#define DLink(d_N, d_P) bool const &b##d_N = b##d_P || b##d_P;
    DLink(1, 0) DLink(2, 1) DLink(3, 2) DLink(4, 3) DLink(5, 4) DLink(6, 5)
    DLink(7, 6) DLink(8, 7) DLink(9, 8) DLink(10, 9) DLink(11, 10)
    DLink(12, 11) DLink(13, 12) DLink(14, 13) DLink(15, 14) DLink(16, 15)
    DLink(17, 16) DLink(18, 17) DLink(19, 18) DLink(20, 19) DLink(21, 20)
    DLink(22, 21) DLink(23, 22) DLink(24, 23) DLink(25, 24) DLink(26, 25)
    DLink(27, 26) DLink(28, 27) DLink(29, 28) DLink(30, 29)
#undef DLink
    auto Resume = co_await fg_OnResume([&]() -> CExceptionPointer {
      if (b30)
        return fg_MakeError("Shutting down");
      return {};
    });
    int &Value = mp_Items[1];
    co_await fg_Work();
    co_return Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'Value' refers into the data of the actor and is used after a suspension; 'mp_Items' loses entries only in Destroy, so a guard of that phase that is alive at the use, like f_CheckDestroyedOnResume, makes it safe [malterlib-coroutine-actor-reference]
  }
};
}

namespace NRound237 {
TCFuture<int> fg_Work();
struct CEntry {
  CStr m_Name;
  CStr const &f_GetName() const { return m_Name; }
};
struct CApp : CPhaseActor {
  DMibActorEntries(Destroy) TCMap<int, CEntry> mp_Items;
  // What a getter of an entry gives of it, which the guard keeps.
  TCFuture<int> f_Read() {
    auto Guard = co_await f_CheckDestroyedOnResume();
    CStr const &Name = mp_Items[1].f_GetName();
    co_await fg_Work();
    co_return Name < CStr("peer");
  }
};
struct CValue {
  int m_Value;
};
struct CWorker : CActor {
  DMibActorEntries(Owner) TCMap<CStr, CValue> mp_Items;
  // An entry made anew each time a loop runs, and removed before the next.
  TCFuture<int> f_Work(int _nCount) {
    for (int i = 0; i != _nCount; ++i) {
      auto Key = fg_RandomID(mp_Items);
      auto &Value = mp_Items[Key];
      co_await fg_Work();
      Value.m_Value = 42;
      mp_Items.f_Remove(Key);
    }
    co_return 0;
  }
  // Not one made before the loop.
  TCFuture<int> f_Before(int _nCount) {
    auto Key = fg_RandomID(mp_Items);
    auto &Value = mp_Items[Key];
    for (int i = 0; i != _nCount; ++i) {
      co_await fg_Work();
      Value.m_Value = 42;
// CHECK-MESSAGES: :[[@LINE-1]]:7: warning: 'Value' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
      mp_Items.f_Remove(Key);
    }
    co_return 0;
  }
};
}

namespace NRound238 {
TCFuture<int> fg_Work();
struct CEntry {
  int m_Value;
  TCSharedPointer<bool> m_pDestroyed;
  ~CEntry() { *m_pDestroyed = true; }
};
void fg_Clear(TCMap<int, int> &_Items) {
  for (auto iEntry = _Items.f_GetIterator(); iEntry;)
    iEntry.f_Remove();
}
struct CApp : CActor {
  TCMap<int, CEntry> mp_Items;
  TCMap<int, int> mp_Values;
  // A scope exit run early that removes entries, after the flag is tested.
  TCFuture<int> f_Run() {
    auto &Entry = mp_Items[1];
    auto pDestroyed = Entry.m_pDestroyed;
    co_await fg_Work();
    if (*pDestroyed)
      co_return 0;
    auto Cleanup = g_OnScopeExit / [this] { mp_Items.f_Clear(); };
    Cleanup();
    co_return Entry.m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'Entry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  // A helper that removes through an iterator of what it is given.
  TCFuture<int> f_Helper() {
    int *pEntry = nullptr;
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = &mp_Values[1];
      fg_Clear(mp_Values);
      return {};
    });
    co_await fg_Work();
    co_return *pEntry;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
};
}

namespace NRound239 {
TCFuture<int> fg_Work();
TCSharedPointer<bool> fg_Construct(bool _bValue);
struct CEntry {
  ~CEntry() { *m_pDeleted = true; }
  TCSharedPointer<bool> m_pDeleted = fg_Construct(false);
  int m_Value;
};
struct CState {
  TCMap<int, CEntry> m_Items;
};
// A const virtual call after the flag is tested, which an override can make
// free through what the object owns.
struct CCache : CActor {
  TCSharedPointer<CState> mp_pState;
  virtual int f_GetStatistics() const = 0;
  TCFuture<int> f_Read() {
    auto *pEntry = mp_pState->m_Items.f_FindEqual(1);
    auto pDeleted = pEntry->m_pDeleted;
    co_await fg_Work();
    if (*pDeleted)
      co_return 0;
    f_GetStatistics();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
};
int fg_CountItems(TCMap<int, int> const *_pItems) {
  return _pItems->f_FindEqual(1) ? 1 : 0;
}
void fg_InspectItems(TCMap<int, int> &_Items) { (void)fg_CountItems(&_Items); }
// A helper that only gives what it is given on by a pointer to const.
struct CApp : CActor {
  TCMap<int, int> mp_Items;
  TCFuture<int> f_Read() {
    int *pValue = mp_Items.f_FindEqual(1);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pValue = mp_Items.f_FindEqual(1);
      fg_InspectItems(mp_Items);
      if (!pValue)
        return fg_MakeError("gone");
      return {};
    });
    co_await fg_Work();
    co_return *pValue;
  }
};
}

namespace NRound240 {
TCFuture<int> fg_Suspend();
TCFuture<int> fg_Work();
struct CEntry {
  int m_Value;
  TCSharedPointer<bool> m_pDeleted;
  ~CEntry() { *m_pDeleted = true; }
};
struct CApp : CActor {
  TCMap<int, CEntry> mp_Items;
  TCMap<int, int> const m_Configuration;
  TCMap<int, int> mp_Values;
  CApp(TCMap<int, int> _Configuration)
      : m_Configuration(fg_Move(_Configuration)) {}
  // Cleared through a captured reference to the data after the flag test.
  TCFuture<int> f_Run() {
    auto *pEntry = mp_Items.f_FindEqual(1);
    if (!pEntry)
      co_return 0;
    auto pDeleted = pEntry->m_pDeleted;
    auto &Items = mp_Items;
    co_await fg_Suspend();
    co_return [pEntry, pDeleted, &Items] {
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
      if (*pDeleted)
        return 0;
      Items.f_Clear();
      return pEntry->m_Value;
    }();
  }
  // A member that is const itself loses no entries.
  TCFuture<int> f_Read() {
    auto const *pValue = m_Configuration.f_FindEqual(1);
    if (!pValue)
      co_return 0;
    co_await fg_Suspend();
    co_return *pValue;
  }
  // A future moved by its member gives what it does.
  TCFuture<int *> f_Find() { co_return mp_Values.f_FindEqual(1); }
  TCFuture<int> f_Moved() {
    auto Find = f_Find();
    int *pValue = co_await Find.f_Move();
    co_await fg_Work();
    co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pValue' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
};
}

namespace NRound241 {
TCFuture<int> fg_Work();
struct CEntry {
  int *m_pValue;
};
TCMap<int, CEntry> fg_MakeIndex(int *_pValue);
// A pointer a const index holds points into data that can be removed.
struct CApp : CActor {
  TCMap<int, int> mp_Values;
  TCMap<int, CEntry> const mp_Index;
  CApp() : mp_Index(fg_MakeIndex(&mp_Values[1])) {}
  TCFuture<int> f_Read() {
    int *pValue = mp_Index.f_FindEqual(1)->m_pValue;
    co_await fg_Work();
    co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pValue' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
};
}

namespace NRound245 {
struct CDefaultAllocator {};
template <typename t_CObject, typename t_CAllocator, typename... tp_CParams>
t_CObject *fg_ConstructObject(t_CAllocator &&, tp_CParams &&...p_Params) {
  return new t_CObject(NMib::fg_Forward<tp_CParams>(p_Params)...);
}
template <typename t_CAllocator, typename t_CObject>
void fg_DeleteObject(t_CAllocator &&, t_CObject *_pObject) { delete _pObject; }
// What a helper allocates is a copy of what it is given, with its own storage.
struct CApp : CActor {
  TCMap<int, int> mp_Items;
  TCFuture<int> f_Copy() {
    auto pCopy = fg_ConstructObject<int>(CDefaultAllocator{}, mp_Items[1]);
    auto Cleanup = g_OnScopeExit / [pCopy] {
      fg_DeleteObject(CDefaultAllocator{}, pCopy);
    };
    co_await CAwaiter{};
    co_return *pCopy;
  }
};
}
