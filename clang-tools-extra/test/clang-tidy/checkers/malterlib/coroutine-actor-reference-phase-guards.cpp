// RUN: %check_clang_tidy -std=c++20-or-later %s malterlib-coroutine-actor-reference %t -- -config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" -- -I %S/Inputs

#include "malterlib-actor-phase.h"

// A phase guard guards only when the object it guards can't be null: a value
// of a conditional that is null, or a `dynamic_cast` to a pointer, through
// any path the object takes, guards nothing then. Only the object it guards
// decides, what it is called on or given of the actor, not another argument,
// like a message or a context.

struct CDownload
{
	int m_Value = 0;
};

TCFuture<int> fg_Suspend();

template <typename t_CType>
t_CType *fg_Identity(t_CType *_pValue)
{
	return _pValue;
}

struct CGuarded : CPhaseActor
{
	DMibActorEntries(Destroy) TCMap<CStr, CDownload> mp_Items;

	DMibActorPhaseGuard(Destroy) static TCFuture<COnResumeScope> fs_Guard(CGuarded *_pThis);
	DMibActorPhaseGuard(Destroy) static TCFuture<COnResumeScope> fs_GuardWithContext(CGuarded *_pThis, void const *_pContext);
	DMibActorPhaseGuard(Destroy) static TCFuture<COnResumeScope> fs_GuardWithMember(CGuarded *_pThis, TCMap<CStr, CDownload> *_pItems);
	DMibActorPhaseGuard(Destroy) TCFuture<COnResumeScope> f_Guard(char const *_pMessage = nullptr) const
	{
		return f_CheckDestroyedOnResume();
	}

	// Accessors that give the object, or null.
	CGuarded *f_Self()
	{
		return this;
	}
	CGuarded *f_MaybeSelf(bool _bKeep)
	{
		return _bKeep ? this : nullptr;
	}

	TCFuture<int> f_GuardedThisDirect(bool _bKeep)
	{
		auto Guard = co_await fs_Guard(this);
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_CalledThisDirect(bool _bKeep)
	{
		auto Guard = co_await (this)->f_Guard();
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_ContextThisDirect(bool _bKeep)
	{
		auto Guard = co_await fs_GuardWithContext(this, this);
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_GuardedThisParenthesized(bool _bKeep)
	{
		auto Guard = co_await fs_Guard(((this)));
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_CalledThisParenthesized(bool _bKeep)
	{
		auto Guard = co_await (((this)))->f_Guard();
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_ContextThisParenthesized(bool _bKeep)
	{
		auto Guard = co_await fs_GuardWithContext(this, ((this)));
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_GuardedThisStaticCast(bool _bKeep)
	{
		auto Guard = co_await fs_Guard(static_cast<CGuarded *>(this));
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_CalledThisStaticCast(bool _bKeep)
	{
		auto Guard = co_await (static_cast<CGuarded *>(this))->f_Guard();
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_ContextThisStaticCast(bool _bKeep)
	{
		auto Guard = co_await fs_GuardWithContext(this, static_cast<CGuarded *>(this));
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_GuardedThisComma(bool _bKeep)
	{
		auto Guard = co_await fs_Guard(((void)0, this));
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_CalledThisComma(bool _bKeep)
	{
		auto Guard = co_await (((void)0, this))->f_Guard();
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_ContextThisComma(bool _bKeep)
	{
		auto Guard = co_await fs_GuardWithContext(this, ((void)0, this));
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_GuardedThisLocal(bool _bKeep)
	{
		CGuarded *pMaybe = this;
		auto Guard = co_await fs_Guard(pMaybe);
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_CalledThisLocal(bool _bKeep)
	{
		CGuarded *pMaybe = this;
		auto Guard = co_await (pMaybe)->f_Guard();
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_ContextThisLocal(bool _bKeep)
	{
		CGuarded *pMaybe = this;
		auto Guard = co_await fs_GuardWithContext(this, pMaybe);
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_GuardedThisConstLocalReread(bool _bKeep)
	{
		CGuarded *const pMaybe = this;
		auto Guard = co_await fs_Guard(*&pMaybe);
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_CalledThisConstLocalReread(bool _bKeep)
	{
		CGuarded *const pMaybe = this;
		auto Guard = co_await (*&pMaybe)->f_Guard();
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_ContextThisConstLocalReread(bool _bKeep)
	{
		CGuarded *const pMaybe = this;
		auto Guard = co_await fs_GuardWithContext(this, *&pMaybe);
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_GuardedThisReferenceAlias(bool _bKeep)
	{
		CGuarded *const &pMaybe = this;
		auto Guard = co_await fs_Guard(pMaybe);
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_CalledThisReferenceAlias(bool _bKeep)
	{
		CGuarded *const &pMaybe = this;
		auto Guard = co_await (pMaybe)->f_Guard();
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_ContextThisReferenceAlias(bool _bKeep)
	{
		CGuarded *const &pMaybe = this;
		auto Guard = co_await fs_GuardWithContext(this, pMaybe);
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_GuardedThisIdentity(bool _bKeep)
	{
		auto Guard = co_await fs_Guard(fg_Identity(this));
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_CalledThisIdentity(bool _bKeep)
	{
		auto Guard = co_await (fg_Identity(this))->f_Guard();
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_ContextThisIdentity(bool _bKeep)
	{
		auto Guard = co_await fs_GuardWithContext(this, fg_Identity(this));
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_GuardedThisMoved(bool _bKeep)
	{
		auto Guard = co_await fs_Guard(fg_Move(this));
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_CalledThisMoved(bool _bKeep)
	{
		auto Guard = co_await (fg_Move(this))->f_Guard();
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_ContextThisMoved(bool _bKeep)
	{
		auto Guard = co_await fs_GuardWithContext(this, fg_Move(this));
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_GuardedThisAddressOfDereferenced(bool _bKeep)
	{
		CGuarded *pMaybe = this;
		auto Guard = co_await fs_Guard(&*pMaybe);
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_CalledThisAddressOfDereferenced(bool _bKeep)
	{
		CGuarded *pMaybe = this;
		auto Guard = co_await (&*pMaybe)->f_Guard();
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_ContextThisAddressOfDereferenced(bool _bKeep)
	{
		CGuarded *pMaybe = this;
		auto Guard = co_await fs_GuardWithContext(this, &*pMaybe);
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_GuardedBothThisDirect(bool _bKeep)
	{
		auto Guard = co_await fs_Guard((_bKeep ? this : this));
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_CalledBothThisDirect(bool _bKeep)
	{
		auto Guard = co_await ((_bKeep ? this : this))->f_Guard();
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_ContextBothThisDirect(bool _bKeep)
	{
		auto Guard = co_await fs_GuardWithContext(this, (_bKeep ? this : this));
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_GuardedBothThisParenthesized(bool _bKeep)
	{
		auto Guard = co_await fs_Guard((((_bKeep ? this : this))));
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_CalledBothThisParenthesized(bool _bKeep)
	{
		auto Guard = co_await ((((_bKeep ? this : this))))->f_Guard();
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_ContextBothThisParenthesized(bool _bKeep)
	{
		auto Guard = co_await fs_GuardWithContext(this, (((_bKeep ? this : this))));
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_GuardedBothThisStaticCast(bool _bKeep)
	{
		auto Guard = co_await fs_Guard(static_cast<CGuarded *>((_bKeep ? this : this)));
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_CalledBothThisStaticCast(bool _bKeep)
	{
		auto Guard = co_await (static_cast<CGuarded *>((_bKeep ? this : this)))->f_Guard();
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_ContextBothThisStaticCast(bool _bKeep)
	{
		auto Guard = co_await fs_GuardWithContext(this, static_cast<CGuarded *>((_bKeep ? this : this)));
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_GuardedBothThisComma(bool _bKeep)
	{
		auto Guard = co_await fs_Guard(((void)0, (_bKeep ? this : this)));
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_CalledBothThisComma(bool _bKeep)
	{
		auto Guard = co_await (((void)0, (_bKeep ? this : this)))->f_Guard();
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_ContextBothThisComma(bool _bKeep)
	{
		auto Guard = co_await fs_GuardWithContext(this, ((void)0, (_bKeep ? this : this)));
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_GuardedBothThisLocal(bool _bKeep)
	{
		CGuarded *pMaybe = (_bKeep ? this : this);
		auto Guard = co_await fs_Guard(pMaybe);
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_CalledBothThisLocal(bool _bKeep)
	{
		CGuarded *pMaybe = (_bKeep ? this : this);
		auto Guard = co_await (pMaybe)->f_Guard();
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_ContextBothThisLocal(bool _bKeep)
	{
		CGuarded *pMaybe = (_bKeep ? this : this);
		auto Guard = co_await fs_GuardWithContext(this, pMaybe);
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_GuardedBothThisConstLocalReread(bool _bKeep)
	{
		CGuarded *const pMaybe = (_bKeep ? this : this);
		auto Guard = co_await fs_Guard(*&pMaybe);
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_CalledBothThisConstLocalReread(bool _bKeep)
	{
		CGuarded *const pMaybe = (_bKeep ? this : this);
		auto Guard = co_await (*&pMaybe)->f_Guard();
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_ContextBothThisConstLocalReread(bool _bKeep)
	{
		CGuarded *const pMaybe = (_bKeep ? this : this);
		auto Guard = co_await fs_GuardWithContext(this, *&pMaybe);
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_GuardedBothThisReferenceAlias(bool _bKeep)
	{
		CGuarded *const &pMaybe = (_bKeep ? this : this);
		auto Guard = co_await fs_Guard(pMaybe);
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_CalledBothThisReferenceAlias(bool _bKeep)
	{
		CGuarded *const &pMaybe = (_bKeep ? this : this);
		auto Guard = co_await (pMaybe)->f_Guard();
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_ContextBothThisReferenceAlias(bool _bKeep)
	{
		CGuarded *const &pMaybe = (_bKeep ? this : this);
		auto Guard = co_await fs_GuardWithContext(this, pMaybe);
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_GuardedBothThisIdentity(bool _bKeep)
	{
		auto Guard = co_await fs_Guard(fg_Identity((_bKeep ? this : this)));
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_CalledBothThisIdentity(bool _bKeep)
	{
		auto Guard = co_await (fg_Identity((_bKeep ? this : this)))->f_Guard();
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_ContextBothThisIdentity(bool _bKeep)
	{
		auto Guard = co_await fs_GuardWithContext(this, fg_Identity((_bKeep ? this : this)));
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_GuardedBothThisMoved(bool _bKeep)
	{
		auto Guard = co_await fs_Guard(fg_Move((_bKeep ? this : this)));
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_CalledBothThisMoved(bool _bKeep)
	{
		auto Guard = co_await (fg_Move((_bKeep ? this : this)))->f_Guard();
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_ContextBothThisMoved(bool _bKeep)
	{
		auto Guard = co_await fs_GuardWithContext(this, fg_Move((_bKeep ? this : this)));
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_GuardedBothThisAddressOfDereferenced(bool _bKeep)
	{
		CGuarded *pMaybe = (_bKeep ? this : this);
		auto Guard = co_await fs_Guard(&*pMaybe);
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_CalledBothThisAddressOfDereferenced(bool _bKeep)
	{
		CGuarded *pMaybe = (_bKeep ? this : this);
		auto Guard = co_await (&*pMaybe)->f_Guard();
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_ContextBothThisAddressOfDereferenced(bool _bKeep)
	{
		CGuarded *pMaybe = (_bKeep ? this : this);
		auto Guard = co_await fs_GuardWithContext(this, &*pMaybe);
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_GuardedReferenceDirect(bool _bKeep)
	{
		CGuarded &Self = *this;
		auto Guard = co_await fs_Guard(&Self);
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_CalledReferenceDirect(bool _bKeep)
	{
		CGuarded &Self = *this;
		auto Guard = co_await (&Self)->f_Guard();
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_ContextReferenceDirect(bool _bKeep)
	{
		CGuarded &Self = *this;
		auto Guard = co_await fs_GuardWithContext(this, &Self);
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_GuardedReferenceParenthesized(bool _bKeep)
	{
		CGuarded &Self = *this;
		auto Guard = co_await fs_Guard(((&Self)));
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_CalledReferenceParenthesized(bool _bKeep)
	{
		CGuarded &Self = *this;
		auto Guard = co_await (((&Self)))->f_Guard();
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_ContextReferenceParenthesized(bool _bKeep)
	{
		CGuarded &Self = *this;
		auto Guard = co_await fs_GuardWithContext(this, ((&Self)));
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_GuardedReferenceStaticCast(bool _bKeep)
	{
		CGuarded &Self = *this;
		auto Guard = co_await fs_Guard(static_cast<CGuarded *>(&Self));
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_CalledReferenceStaticCast(bool _bKeep)
	{
		CGuarded &Self = *this;
		auto Guard = co_await (static_cast<CGuarded *>(&Self))->f_Guard();
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_ContextReferenceStaticCast(bool _bKeep)
	{
		CGuarded &Self = *this;
		auto Guard = co_await fs_GuardWithContext(this, static_cast<CGuarded *>(&Self));
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_GuardedReferenceComma(bool _bKeep)
	{
		CGuarded &Self = *this;
		auto Guard = co_await fs_Guard(((void)0, &Self));
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_CalledReferenceComma(bool _bKeep)
	{
		CGuarded &Self = *this;
		auto Guard = co_await (((void)0, &Self))->f_Guard();
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_ContextReferenceComma(bool _bKeep)
	{
		CGuarded &Self = *this;
		auto Guard = co_await fs_GuardWithContext(this, ((void)0, &Self));
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_GuardedReferenceLocal(bool _bKeep)
	{
		CGuarded &Self = *this;
		CGuarded *pMaybe = &Self;
		auto Guard = co_await fs_Guard(pMaybe);
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_CalledReferenceLocal(bool _bKeep)
	{
		CGuarded &Self = *this;
		CGuarded *pMaybe = &Self;
		auto Guard = co_await (pMaybe)->f_Guard();
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_ContextReferenceLocal(bool _bKeep)
	{
		CGuarded &Self = *this;
		CGuarded *pMaybe = &Self;
		auto Guard = co_await fs_GuardWithContext(this, pMaybe);
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_GuardedReferenceConstLocalReread(bool _bKeep)
	{
		CGuarded &Self = *this;
		CGuarded *const pMaybe = &Self;
		auto Guard = co_await fs_Guard(*&pMaybe);
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_CalledReferenceConstLocalReread(bool _bKeep)
	{
		CGuarded &Self = *this;
		CGuarded *const pMaybe = &Self;
		auto Guard = co_await (*&pMaybe)->f_Guard();
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_ContextReferenceConstLocalReread(bool _bKeep)
	{
		CGuarded &Self = *this;
		CGuarded *const pMaybe = &Self;
		auto Guard = co_await fs_GuardWithContext(this, *&pMaybe);
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_GuardedReferenceReferenceAlias(bool _bKeep)
	{
		CGuarded &Self = *this;
		CGuarded *const &pMaybe = &Self;
		auto Guard = co_await fs_Guard(pMaybe);
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_CalledReferenceReferenceAlias(bool _bKeep)
	{
		CGuarded &Self = *this;
		CGuarded *const &pMaybe = &Self;
		auto Guard = co_await (pMaybe)->f_Guard();
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_ContextReferenceReferenceAlias(bool _bKeep)
	{
		CGuarded &Self = *this;
		CGuarded *const &pMaybe = &Self;
		auto Guard = co_await fs_GuardWithContext(this, pMaybe);
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_GuardedReferenceIdentity(bool _bKeep)
	{
		CGuarded &Self = *this;
		auto Guard = co_await fs_Guard(fg_Identity(&Self));
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_CalledReferenceIdentity(bool _bKeep)
	{
		CGuarded &Self = *this;
		auto Guard = co_await (fg_Identity(&Self))->f_Guard();
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_ContextReferenceIdentity(bool _bKeep)
	{
		CGuarded &Self = *this;
		auto Guard = co_await fs_GuardWithContext(this, fg_Identity(&Self));
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_GuardedReferenceMoved(bool _bKeep)
	{
		CGuarded &Self = *this;
		auto Guard = co_await fs_Guard(fg_Move(&Self));
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_CalledReferenceMoved(bool _bKeep)
	{
		CGuarded &Self = *this;
		auto Guard = co_await (fg_Move(&Self))->f_Guard();
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_ContextReferenceMoved(bool _bKeep)
	{
		CGuarded &Self = *this;
		auto Guard = co_await fs_GuardWithContext(this, fg_Move(&Self));
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_GuardedReferenceAddressOfDereferenced(bool _bKeep)
	{
		CGuarded &Self = *this;
		CGuarded *pMaybe = &Self;
		auto Guard = co_await fs_Guard(&*pMaybe);
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_CalledReferenceAddressOfDereferenced(bool _bKeep)
	{
		CGuarded &Self = *this;
		CGuarded *pMaybe = &Self;
		auto Guard = co_await (&*pMaybe)->f_Guard();
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_ContextReferenceAddressOfDereferenced(bool _bKeep)
	{
		CGuarded &Self = *this;
		CGuarded *pMaybe = &Self;
		auto Guard = co_await fs_GuardWithContext(this, &*pMaybe);
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_GuardedNullBranchDirect(bool _bKeep)
	{
		auto Guard = co_await fs_Guard((_bKeep ? this : nullptr));
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'pEntry' refers into the data of the actor and is used after a suspension; 'mp_Items' loses entries only in Destroy, so a guard of that phase that is alive at the use, like f_CheckDestroyedOnResume, makes it safe [malterlib-coroutine-actor-reference]
	}

	TCFuture<int> f_CalledNullBranchDirect(bool _bKeep)
	{
		auto Guard = co_await ((_bKeep ? this : nullptr))->f_Guard();
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'pEntry' refers into the data of the actor and is used after a suspension; 'mp_Items' loses entries only in Destroy, so a guard of that phase that is alive at the use, like f_CheckDestroyedOnResume, makes it safe [malterlib-coroutine-actor-reference]
	}

	TCFuture<int> f_ContextNullBranchDirect(bool _bKeep)
	{
		auto Guard = co_await fs_GuardWithContext(this, (_bKeep ? this : nullptr));
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_GuardedNullBranchParenthesized(bool _bKeep)
	{
		auto Guard = co_await fs_Guard((((_bKeep ? this : nullptr))));
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'pEntry' refers into the data of the actor and is used after a suspension; 'mp_Items' loses entries only in Destroy, so a guard of that phase that is alive at the use, like f_CheckDestroyedOnResume, makes it safe [malterlib-coroutine-actor-reference]
	}

	TCFuture<int> f_CalledNullBranchParenthesized(bool _bKeep)
	{
		auto Guard = co_await ((((_bKeep ? this : nullptr))))->f_Guard();
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'pEntry' refers into the data of the actor and is used after a suspension; 'mp_Items' loses entries only in Destroy, so a guard of that phase that is alive at the use, like f_CheckDestroyedOnResume, makes it safe [malterlib-coroutine-actor-reference]
	}

	TCFuture<int> f_ContextNullBranchParenthesized(bool _bKeep)
	{
		auto Guard = co_await fs_GuardWithContext(this, (((_bKeep ? this : nullptr))));
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_GuardedNullBranchStaticCast(bool _bKeep)
	{
		auto Guard = co_await fs_Guard(static_cast<CGuarded *>((_bKeep ? this : nullptr)));
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'pEntry' refers into the data of the actor and is used after a suspension; 'mp_Items' loses entries only in Destroy, so a guard of that phase that is alive at the use, like f_CheckDestroyedOnResume, makes it safe [malterlib-coroutine-actor-reference]
	}

	TCFuture<int> f_CalledNullBranchStaticCast(bool _bKeep)
	{
		auto Guard = co_await (static_cast<CGuarded *>((_bKeep ? this : nullptr)))->f_Guard();
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'pEntry' refers into the data of the actor and is used after a suspension; 'mp_Items' loses entries only in Destroy, so a guard of that phase that is alive at the use, like f_CheckDestroyedOnResume, makes it safe [malterlib-coroutine-actor-reference]
	}

	TCFuture<int> f_ContextNullBranchStaticCast(bool _bKeep)
	{
		auto Guard = co_await fs_GuardWithContext(this, static_cast<CGuarded *>((_bKeep ? this : nullptr)));
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_GuardedNullBranchComma(bool _bKeep)
	{
		auto Guard = co_await fs_Guard(((void)0, (_bKeep ? this : nullptr)));
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'pEntry' refers into the data of the actor and is used after a suspension; 'mp_Items' loses entries only in Destroy, so a guard of that phase that is alive at the use, like f_CheckDestroyedOnResume, makes it safe [malterlib-coroutine-actor-reference]
	}

	TCFuture<int> f_CalledNullBranchComma(bool _bKeep)
	{
		auto Guard = co_await (((void)0, (_bKeep ? this : nullptr)))->f_Guard();
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'pEntry' refers into the data of the actor and is used after a suspension; 'mp_Items' loses entries only in Destroy, so a guard of that phase that is alive at the use, like f_CheckDestroyedOnResume, makes it safe [malterlib-coroutine-actor-reference]
	}

	TCFuture<int> f_ContextNullBranchComma(bool _bKeep)
	{
		auto Guard = co_await fs_GuardWithContext(this, ((void)0, (_bKeep ? this : nullptr)));
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_GuardedNullBranchLocal(bool _bKeep)
	{
		CGuarded *pMaybe = (_bKeep ? this : nullptr);
		auto Guard = co_await fs_Guard(pMaybe);
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'pEntry' refers into the data of the actor and is used after a suspension; 'mp_Items' loses entries only in Destroy, so a guard of that phase that is alive at the use, like f_CheckDestroyedOnResume, makes it safe [malterlib-coroutine-actor-reference]
	}

	TCFuture<int> f_CalledNullBranchLocal(bool _bKeep)
	{
		CGuarded *pMaybe = (_bKeep ? this : nullptr);
		auto Guard = co_await (pMaybe)->f_Guard();
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'pEntry' refers into the data of the actor and is used after a suspension; 'mp_Items' loses entries only in Destroy, so a guard of that phase that is alive at the use, like f_CheckDestroyedOnResume, makes it safe [malterlib-coroutine-actor-reference]
	}

	TCFuture<int> f_ContextNullBranchLocal(bool _bKeep)
	{
		CGuarded *pMaybe = (_bKeep ? this : nullptr);
		auto Guard = co_await fs_GuardWithContext(this, pMaybe);
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_GuardedNullBranchConstLocalReread(bool _bKeep)
	{
		CGuarded *const pMaybe = (_bKeep ? this : nullptr);
		auto Guard = co_await fs_Guard(*&pMaybe);
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'pEntry' refers into the data of the actor and is used after a suspension; 'mp_Items' loses entries only in Destroy, so a guard of that phase that is alive at the use, like f_CheckDestroyedOnResume, makes it safe [malterlib-coroutine-actor-reference]
	}

	TCFuture<int> f_CalledNullBranchConstLocalReread(bool _bKeep)
	{
		CGuarded *const pMaybe = (_bKeep ? this : nullptr);
		auto Guard = co_await (*&pMaybe)->f_Guard();
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'pEntry' refers into the data of the actor and is used after a suspension; 'mp_Items' loses entries only in Destroy, so a guard of that phase that is alive at the use, like f_CheckDestroyedOnResume, makes it safe [malterlib-coroutine-actor-reference]
	}

	TCFuture<int> f_ContextNullBranchConstLocalReread(bool _bKeep)
	{
		CGuarded *const pMaybe = (_bKeep ? this : nullptr);
		auto Guard = co_await fs_GuardWithContext(this, *&pMaybe);
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_GuardedNullBranchReferenceAlias(bool _bKeep)
	{
		CGuarded *const &pMaybe = (_bKeep ? this : nullptr);
		auto Guard = co_await fs_Guard(pMaybe);
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'pEntry' refers into the data of the actor and is used after a suspension; 'mp_Items' loses entries only in Destroy, so a guard of that phase that is alive at the use, like f_CheckDestroyedOnResume, makes it safe [malterlib-coroutine-actor-reference]
	}

	TCFuture<int> f_CalledNullBranchReferenceAlias(bool _bKeep)
	{
		CGuarded *const &pMaybe = (_bKeep ? this : nullptr);
		auto Guard = co_await (pMaybe)->f_Guard();
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'pEntry' refers into the data of the actor and is used after a suspension; 'mp_Items' loses entries only in Destroy, so a guard of that phase that is alive at the use, like f_CheckDestroyedOnResume, makes it safe [malterlib-coroutine-actor-reference]
	}

	TCFuture<int> f_ContextNullBranchReferenceAlias(bool _bKeep)
	{
		CGuarded *const &pMaybe = (_bKeep ? this : nullptr);
		auto Guard = co_await fs_GuardWithContext(this, pMaybe);
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_GuardedNullBranchIdentity(bool _bKeep)
	{
		auto Guard = co_await fs_Guard(fg_Identity((_bKeep ? this : nullptr)));
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'pEntry' refers into the data of the actor and is used after a suspension; 'mp_Items' loses entries only in Destroy, so a guard of that phase that is alive at the use, like f_CheckDestroyedOnResume, makes it safe [malterlib-coroutine-actor-reference]
	}

	TCFuture<int> f_CalledNullBranchIdentity(bool _bKeep)
	{
		auto Guard = co_await (fg_Identity((_bKeep ? this : nullptr)))->f_Guard();
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'pEntry' refers into the data of the actor and is used after a suspension; 'mp_Items' loses entries only in Destroy, so a guard of that phase that is alive at the use, like f_CheckDestroyedOnResume, makes it safe [malterlib-coroutine-actor-reference]
	}

	TCFuture<int> f_ContextNullBranchIdentity(bool _bKeep)
	{
		auto Guard = co_await fs_GuardWithContext(this, fg_Identity((_bKeep ? this : nullptr)));
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_GuardedNullBranchMoved(bool _bKeep)
	{
		auto Guard = co_await fs_Guard(fg_Move((_bKeep ? this : nullptr)));
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'pEntry' refers into the data of the actor and is used after a suspension; 'mp_Items' loses entries only in Destroy, so a guard of that phase that is alive at the use, like f_CheckDestroyedOnResume, makes it safe [malterlib-coroutine-actor-reference]
	}

	TCFuture<int> f_CalledNullBranchMoved(bool _bKeep)
	{
		auto Guard = co_await (fg_Move((_bKeep ? this : nullptr)))->f_Guard();
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'pEntry' refers into the data of the actor and is used after a suspension; 'mp_Items' loses entries only in Destroy, so a guard of that phase that is alive at the use, like f_CheckDestroyedOnResume, makes it safe [malterlib-coroutine-actor-reference]
	}

	TCFuture<int> f_ContextNullBranchMoved(bool _bKeep)
	{
		auto Guard = co_await fs_GuardWithContext(this, fg_Move((_bKeep ? this : nullptr)));
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_GuardedNullBranchAddressOfDereferenced(bool _bKeep)
	{
		CGuarded *pMaybe = (_bKeep ? this : nullptr);
		auto Guard = co_await fs_Guard(&*pMaybe);
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'pEntry' refers into the data of the actor and is used after a suspension; 'mp_Items' loses entries only in Destroy, so a guard of that phase that is alive at the use, like f_CheckDestroyedOnResume, makes it safe [malterlib-coroutine-actor-reference]
	}

	TCFuture<int> f_CalledNullBranchAddressOfDereferenced(bool _bKeep)
	{
		CGuarded *pMaybe = (_bKeep ? this : nullptr);
		auto Guard = co_await (&*pMaybe)->f_Guard();
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'pEntry' refers into the data of the actor and is used after a suspension; 'mp_Items' loses entries only in Destroy, so a guard of that phase that is alive at the use, like f_CheckDestroyedOnResume, makes it safe [malterlib-coroutine-actor-reference]
	}

	TCFuture<int> f_ContextNullBranchAddressOfDereferenced(bool _bKeep)
	{
		CGuarded *pMaybe = (_bKeep ? this : nullptr);
		auto Guard = co_await fs_GuardWithContext(this, &*pMaybe);
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_GuardedNullFirstDirect(bool _bKeep)
	{
		auto Guard = co_await fs_Guard((_bKeep ? nullptr : this));
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'pEntry' refers into the data of the actor and is used after a suspension; 'mp_Items' loses entries only in Destroy, so a guard of that phase that is alive at the use, like f_CheckDestroyedOnResume, makes it safe [malterlib-coroutine-actor-reference]
	}

	TCFuture<int> f_CalledNullFirstDirect(bool _bKeep)
	{
		auto Guard = co_await ((_bKeep ? nullptr : this))->f_Guard();
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'pEntry' refers into the data of the actor and is used after a suspension; 'mp_Items' loses entries only in Destroy, so a guard of that phase that is alive at the use, like f_CheckDestroyedOnResume, makes it safe [malterlib-coroutine-actor-reference]
	}

	TCFuture<int> f_ContextNullFirstDirect(bool _bKeep)
	{
		auto Guard = co_await fs_GuardWithContext(this, (_bKeep ? nullptr : this));
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_GuardedNullFirstParenthesized(bool _bKeep)
	{
		auto Guard = co_await fs_Guard((((_bKeep ? nullptr : this))));
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'pEntry' refers into the data of the actor and is used after a suspension; 'mp_Items' loses entries only in Destroy, so a guard of that phase that is alive at the use, like f_CheckDestroyedOnResume, makes it safe [malterlib-coroutine-actor-reference]
	}

	TCFuture<int> f_CalledNullFirstParenthesized(bool _bKeep)
	{
		auto Guard = co_await ((((_bKeep ? nullptr : this))))->f_Guard();
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'pEntry' refers into the data of the actor and is used after a suspension; 'mp_Items' loses entries only in Destroy, so a guard of that phase that is alive at the use, like f_CheckDestroyedOnResume, makes it safe [malterlib-coroutine-actor-reference]
	}

	TCFuture<int> f_ContextNullFirstParenthesized(bool _bKeep)
	{
		auto Guard = co_await fs_GuardWithContext(this, (((_bKeep ? nullptr : this))));
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_GuardedNullFirstStaticCast(bool _bKeep)
	{
		auto Guard = co_await fs_Guard(static_cast<CGuarded *>((_bKeep ? nullptr : this)));
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'pEntry' refers into the data of the actor and is used after a suspension; 'mp_Items' loses entries only in Destroy, so a guard of that phase that is alive at the use, like f_CheckDestroyedOnResume, makes it safe [malterlib-coroutine-actor-reference]
	}

	TCFuture<int> f_CalledNullFirstStaticCast(bool _bKeep)
	{
		auto Guard = co_await (static_cast<CGuarded *>((_bKeep ? nullptr : this)))->f_Guard();
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'pEntry' refers into the data of the actor and is used after a suspension; 'mp_Items' loses entries only in Destroy, so a guard of that phase that is alive at the use, like f_CheckDestroyedOnResume, makes it safe [malterlib-coroutine-actor-reference]
	}

	TCFuture<int> f_ContextNullFirstStaticCast(bool _bKeep)
	{
		auto Guard = co_await fs_GuardWithContext(this, static_cast<CGuarded *>((_bKeep ? nullptr : this)));
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_GuardedNullFirstComma(bool _bKeep)
	{
		auto Guard = co_await fs_Guard(((void)0, (_bKeep ? nullptr : this)));
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'pEntry' refers into the data of the actor and is used after a suspension; 'mp_Items' loses entries only in Destroy, so a guard of that phase that is alive at the use, like f_CheckDestroyedOnResume, makes it safe [malterlib-coroutine-actor-reference]
	}

	TCFuture<int> f_CalledNullFirstComma(bool _bKeep)
	{
		auto Guard = co_await (((void)0, (_bKeep ? nullptr : this)))->f_Guard();
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'pEntry' refers into the data of the actor and is used after a suspension; 'mp_Items' loses entries only in Destroy, so a guard of that phase that is alive at the use, like f_CheckDestroyedOnResume, makes it safe [malterlib-coroutine-actor-reference]
	}

	TCFuture<int> f_ContextNullFirstComma(bool _bKeep)
	{
		auto Guard = co_await fs_GuardWithContext(this, ((void)0, (_bKeep ? nullptr : this)));
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_GuardedNullFirstLocal(bool _bKeep)
	{
		CGuarded *pMaybe = (_bKeep ? nullptr : this);
		auto Guard = co_await fs_Guard(pMaybe);
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'pEntry' refers into the data of the actor and is used after a suspension; 'mp_Items' loses entries only in Destroy, so a guard of that phase that is alive at the use, like f_CheckDestroyedOnResume, makes it safe [malterlib-coroutine-actor-reference]
	}

	TCFuture<int> f_CalledNullFirstLocal(bool _bKeep)
	{
		CGuarded *pMaybe = (_bKeep ? nullptr : this);
		auto Guard = co_await (pMaybe)->f_Guard();
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'pEntry' refers into the data of the actor and is used after a suspension; 'mp_Items' loses entries only in Destroy, so a guard of that phase that is alive at the use, like f_CheckDestroyedOnResume, makes it safe [malterlib-coroutine-actor-reference]
	}

	TCFuture<int> f_ContextNullFirstLocal(bool _bKeep)
	{
		CGuarded *pMaybe = (_bKeep ? nullptr : this);
		auto Guard = co_await fs_GuardWithContext(this, pMaybe);
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_GuardedNullFirstConstLocalReread(bool _bKeep)
	{
		CGuarded *const pMaybe = (_bKeep ? nullptr : this);
		auto Guard = co_await fs_Guard(*&pMaybe);
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'pEntry' refers into the data of the actor and is used after a suspension; 'mp_Items' loses entries only in Destroy, so a guard of that phase that is alive at the use, like f_CheckDestroyedOnResume, makes it safe [malterlib-coroutine-actor-reference]
	}

	TCFuture<int> f_CalledNullFirstConstLocalReread(bool _bKeep)
	{
		CGuarded *const pMaybe = (_bKeep ? nullptr : this);
		auto Guard = co_await (*&pMaybe)->f_Guard();
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'pEntry' refers into the data of the actor and is used after a suspension; 'mp_Items' loses entries only in Destroy, so a guard of that phase that is alive at the use, like f_CheckDestroyedOnResume, makes it safe [malterlib-coroutine-actor-reference]
	}

	TCFuture<int> f_ContextNullFirstConstLocalReread(bool _bKeep)
	{
		CGuarded *const pMaybe = (_bKeep ? nullptr : this);
		auto Guard = co_await fs_GuardWithContext(this, *&pMaybe);
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_GuardedNullFirstReferenceAlias(bool _bKeep)
	{
		CGuarded *const &pMaybe = (_bKeep ? nullptr : this);
		auto Guard = co_await fs_Guard(pMaybe);
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'pEntry' refers into the data of the actor and is used after a suspension; 'mp_Items' loses entries only in Destroy, so a guard of that phase that is alive at the use, like f_CheckDestroyedOnResume, makes it safe [malterlib-coroutine-actor-reference]
	}

	TCFuture<int> f_CalledNullFirstReferenceAlias(bool _bKeep)
	{
		CGuarded *const &pMaybe = (_bKeep ? nullptr : this);
		auto Guard = co_await (pMaybe)->f_Guard();
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'pEntry' refers into the data of the actor and is used after a suspension; 'mp_Items' loses entries only in Destroy, so a guard of that phase that is alive at the use, like f_CheckDestroyedOnResume, makes it safe [malterlib-coroutine-actor-reference]
	}

	TCFuture<int> f_ContextNullFirstReferenceAlias(bool _bKeep)
	{
		CGuarded *const &pMaybe = (_bKeep ? nullptr : this);
		auto Guard = co_await fs_GuardWithContext(this, pMaybe);
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_GuardedNullFirstIdentity(bool _bKeep)
	{
		auto Guard = co_await fs_Guard(fg_Identity((_bKeep ? nullptr : this)));
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'pEntry' refers into the data of the actor and is used after a suspension; 'mp_Items' loses entries only in Destroy, so a guard of that phase that is alive at the use, like f_CheckDestroyedOnResume, makes it safe [malterlib-coroutine-actor-reference]
	}

	TCFuture<int> f_CalledNullFirstIdentity(bool _bKeep)
	{
		auto Guard = co_await (fg_Identity((_bKeep ? nullptr : this)))->f_Guard();
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'pEntry' refers into the data of the actor and is used after a suspension; 'mp_Items' loses entries only in Destroy, so a guard of that phase that is alive at the use, like f_CheckDestroyedOnResume, makes it safe [malterlib-coroutine-actor-reference]
	}

	TCFuture<int> f_ContextNullFirstIdentity(bool _bKeep)
	{
		auto Guard = co_await fs_GuardWithContext(this, fg_Identity((_bKeep ? nullptr : this)));
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_GuardedNullFirstMoved(bool _bKeep)
	{
		auto Guard = co_await fs_Guard(fg_Move((_bKeep ? nullptr : this)));
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'pEntry' refers into the data of the actor and is used after a suspension; 'mp_Items' loses entries only in Destroy, so a guard of that phase that is alive at the use, like f_CheckDestroyedOnResume, makes it safe [malterlib-coroutine-actor-reference]
	}

	TCFuture<int> f_CalledNullFirstMoved(bool _bKeep)
	{
		auto Guard = co_await (fg_Move((_bKeep ? nullptr : this)))->f_Guard();
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'pEntry' refers into the data of the actor and is used after a suspension; 'mp_Items' loses entries only in Destroy, so a guard of that phase that is alive at the use, like f_CheckDestroyedOnResume, makes it safe [malterlib-coroutine-actor-reference]
	}

	TCFuture<int> f_ContextNullFirstMoved(bool _bKeep)
	{
		auto Guard = co_await fs_GuardWithContext(this, fg_Move((_bKeep ? nullptr : this)));
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_GuardedNullFirstAddressOfDereferenced(bool _bKeep)
	{
		CGuarded *pMaybe = (_bKeep ? nullptr : this);
		auto Guard = co_await fs_Guard(&*pMaybe);
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'pEntry' refers into the data of the actor and is used after a suspension; 'mp_Items' loses entries only in Destroy, so a guard of that phase that is alive at the use, like f_CheckDestroyedOnResume, makes it safe [malterlib-coroutine-actor-reference]
	}

	TCFuture<int> f_CalledNullFirstAddressOfDereferenced(bool _bKeep)
	{
		CGuarded *pMaybe = (_bKeep ? nullptr : this);
		auto Guard = co_await (&*pMaybe)->f_Guard();
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'pEntry' refers into the data of the actor and is used after a suspension; 'mp_Items' loses entries only in Destroy, so a guard of that phase that is alive at the use, like f_CheckDestroyedOnResume, makes it safe [malterlib-coroutine-actor-reference]
	}

	TCFuture<int> f_ContextNullFirstAddressOfDereferenced(bool _bKeep)
	{
		CGuarded *pMaybe = (_bKeep ? nullptr : this);
		auto Guard = co_await fs_GuardWithContext(this, &*pMaybe);
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_GuardedDynamicCastDirect(bool _bKeep)
	{
		auto Guard = co_await fs_Guard(dynamic_cast<CGuarded *>(static_cast<CPhaseActor *>(this)));
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'pEntry' refers into the data of the actor and is used after a suspension; 'mp_Items' loses entries only in Destroy, so a guard of that phase that is alive at the use, like f_CheckDestroyedOnResume, makes it safe [malterlib-coroutine-actor-reference]
	}

	TCFuture<int> f_CalledDynamicCastDirect(bool _bKeep)
	{
		auto Guard = co_await (dynamic_cast<CGuarded *>(static_cast<CPhaseActor *>(this)))->f_Guard();
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'pEntry' refers into the data of the actor and is used after a suspension; 'mp_Items' loses entries only in Destroy, so a guard of that phase that is alive at the use, like f_CheckDestroyedOnResume, makes it safe [malterlib-coroutine-actor-reference]
	}

	TCFuture<int> f_ContextDynamicCastDirect(bool _bKeep)
	{
		auto Guard = co_await fs_GuardWithContext(this, dynamic_cast<CGuarded *>(static_cast<CPhaseActor *>(this)));
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_GuardedDynamicCastParenthesized(bool _bKeep)
	{
		auto Guard = co_await fs_Guard(((dynamic_cast<CGuarded *>(static_cast<CPhaseActor *>(this)))));
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'pEntry' refers into the data of the actor and is used after a suspension; 'mp_Items' loses entries only in Destroy, so a guard of that phase that is alive at the use, like f_CheckDestroyedOnResume, makes it safe [malterlib-coroutine-actor-reference]
	}

	TCFuture<int> f_CalledDynamicCastParenthesized(bool _bKeep)
	{
		auto Guard = co_await (((dynamic_cast<CGuarded *>(static_cast<CPhaseActor *>(this)))))->f_Guard();
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'pEntry' refers into the data of the actor and is used after a suspension; 'mp_Items' loses entries only in Destroy, so a guard of that phase that is alive at the use, like f_CheckDestroyedOnResume, makes it safe [malterlib-coroutine-actor-reference]
	}

	TCFuture<int> f_ContextDynamicCastParenthesized(bool _bKeep)
	{
		auto Guard = co_await fs_GuardWithContext(this, ((dynamic_cast<CGuarded *>(static_cast<CPhaseActor *>(this)))));
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_GuardedDynamicCastStaticCast(bool _bKeep)
	{
		auto Guard = co_await fs_Guard(static_cast<CGuarded *>(dynamic_cast<CGuarded *>(static_cast<CPhaseActor *>(this))));
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'pEntry' refers into the data of the actor and is used after a suspension; 'mp_Items' loses entries only in Destroy, so a guard of that phase that is alive at the use, like f_CheckDestroyedOnResume, makes it safe [malterlib-coroutine-actor-reference]
	}

	TCFuture<int> f_CalledDynamicCastStaticCast(bool _bKeep)
	{
		auto Guard = co_await (static_cast<CGuarded *>(dynamic_cast<CGuarded *>(static_cast<CPhaseActor *>(this))))->f_Guard();
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'pEntry' refers into the data of the actor and is used after a suspension; 'mp_Items' loses entries only in Destroy, so a guard of that phase that is alive at the use, like f_CheckDestroyedOnResume, makes it safe [malterlib-coroutine-actor-reference]
	}

	TCFuture<int> f_ContextDynamicCastStaticCast(bool _bKeep)
	{
		auto Guard = co_await fs_GuardWithContext(this, static_cast<CGuarded *>(dynamic_cast<CGuarded *>(static_cast<CPhaseActor *>(this))));
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_GuardedDynamicCastComma(bool _bKeep)
	{
		auto Guard = co_await fs_Guard(((void)0, dynamic_cast<CGuarded *>(static_cast<CPhaseActor *>(this))));
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'pEntry' refers into the data of the actor and is used after a suspension; 'mp_Items' loses entries only in Destroy, so a guard of that phase that is alive at the use, like f_CheckDestroyedOnResume, makes it safe [malterlib-coroutine-actor-reference]
	}

	TCFuture<int> f_CalledDynamicCastComma(bool _bKeep)
	{
		auto Guard = co_await (((void)0, dynamic_cast<CGuarded *>(static_cast<CPhaseActor *>(this))))->f_Guard();
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'pEntry' refers into the data of the actor and is used after a suspension; 'mp_Items' loses entries only in Destroy, so a guard of that phase that is alive at the use, like f_CheckDestroyedOnResume, makes it safe [malterlib-coroutine-actor-reference]
	}

	TCFuture<int> f_ContextDynamicCastComma(bool _bKeep)
	{
		auto Guard = co_await fs_GuardWithContext(this, ((void)0, dynamic_cast<CGuarded *>(static_cast<CPhaseActor *>(this))));
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_GuardedDynamicCastLocal(bool _bKeep)
	{
		CGuarded *pMaybe = dynamic_cast<CGuarded *>(static_cast<CPhaseActor *>(this));
		auto Guard = co_await fs_Guard(pMaybe);
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'pEntry' refers into the data of the actor and is used after a suspension; 'mp_Items' loses entries only in Destroy, so a guard of that phase that is alive at the use, like f_CheckDestroyedOnResume, makes it safe [malterlib-coroutine-actor-reference]
	}

	TCFuture<int> f_CalledDynamicCastLocal(bool _bKeep)
	{
		CGuarded *pMaybe = dynamic_cast<CGuarded *>(static_cast<CPhaseActor *>(this));
		auto Guard = co_await (pMaybe)->f_Guard();
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'pEntry' refers into the data of the actor and is used after a suspension; 'mp_Items' loses entries only in Destroy, so a guard of that phase that is alive at the use, like f_CheckDestroyedOnResume, makes it safe [malterlib-coroutine-actor-reference]
	}

	TCFuture<int> f_ContextDynamicCastLocal(bool _bKeep)
	{
		CGuarded *pMaybe = dynamic_cast<CGuarded *>(static_cast<CPhaseActor *>(this));
		auto Guard = co_await fs_GuardWithContext(this, pMaybe);
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_GuardedDynamicCastConstLocalReread(bool _bKeep)
	{
		CGuarded *const pMaybe = dynamic_cast<CGuarded *>(static_cast<CPhaseActor *>(this));
		auto Guard = co_await fs_Guard(*&pMaybe);
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'pEntry' refers into the data of the actor and is used after a suspension; 'mp_Items' loses entries only in Destroy, so a guard of that phase that is alive at the use, like f_CheckDestroyedOnResume, makes it safe [malterlib-coroutine-actor-reference]
	}

	TCFuture<int> f_CalledDynamicCastConstLocalReread(bool _bKeep)
	{
		CGuarded *const pMaybe = dynamic_cast<CGuarded *>(static_cast<CPhaseActor *>(this));
		auto Guard = co_await (*&pMaybe)->f_Guard();
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'pEntry' refers into the data of the actor and is used after a suspension; 'mp_Items' loses entries only in Destroy, so a guard of that phase that is alive at the use, like f_CheckDestroyedOnResume, makes it safe [malterlib-coroutine-actor-reference]
	}

	TCFuture<int> f_ContextDynamicCastConstLocalReread(bool _bKeep)
	{
		CGuarded *const pMaybe = dynamic_cast<CGuarded *>(static_cast<CPhaseActor *>(this));
		auto Guard = co_await fs_GuardWithContext(this, *&pMaybe);
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_GuardedDynamicCastReferenceAlias(bool _bKeep)
	{
		CGuarded *const &pMaybe = dynamic_cast<CGuarded *>(static_cast<CPhaseActor *>(this));
		auto Guard = co_await fs_Guard(pMaybe);
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'pEntry' refers into the data of the actor and is used after a suspension; 'mp_Items' loses entries only in Destroy, so a guard of that phase that is alive at the use, like f_CheckDestroyedOnResume, makes it safe [malterlib-coroutine-actor-reference]
	}

	TCFuture<int> f_CalledDynamicCastReferenceAlias(bool _bKeep)
	{
		CGuarded *const &pMaybe = dynamic_cast<CGuarded *>(static_cast<CPhaseActor *>(this));
		auto Guard = co_await (pMaybe)->f_Guard();
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'pEntry' refers into the data of the actor and is used after a suspension; 'mp_Items' loses entries only in Destroy, so a guard of that phase that is alive at the use, like f_CheckDestroyedOnResume, makes it safe [malterlib-coroutine-actor-reference]
	}

	TCFuture<int> f_ContextDynamicCastReferenceAlias(bool _bKeep)
	{
		CGuarded *const &pMaybe = dynamic_cast<CGuarded *>(static_cast<CPhaseActor *>(this));
		auto Guard = co_await fs_GuardWithContext(this, pMaybe);
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_GuardedDynamicCastIdentity(bool _bKeep)
	{
		auto Guard = co_await fs_Guard(fg_Identity(dynamic_cast<CGuarded *>(static_cast<CPhaseActor *>(this))));
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'pEntry' refers into the data of the actor and is used after a suspension; 'mp_Items' loses entries only in Destroy, so a guard of that phase that is alive at the use, like f_CheckDestroyedOnResume, makes it safe [malterlib-coroutine-actor-reference]
	}

	TCFuture<int> f_CalledDynamicCastIdentity(bool _bKeep)
	{
		auto Guard = co_await (fg_Identity(dynamic_cast<CGuarded *>(static_cast<CPhaseActor *>(this))))->f_Guard();
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'pEntry' refers into the data of the actor and is used after a suspension; 'mp_Items' loses entries only in Destroy, so a guard of that phase that is alive at the use, like f_CheckDestroyedOnResume, makes it safe [malterlib-coroutine-actor-reference]
	}

	TCFuture<int> f_ContextDynamicCastIdentity(bool _bKeep)
	{
		auto Guard = co_await fs_GuardWithContext(this, fg_Identity(dynamic_cast<CGuarded *>(static_cast<CPhaseActor *>(this))));
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_GuardedDynamicCastMoved(bool _bKeep)
	{
		auto Guard = co_await fs_Guard(fg_Move(dynamic_cast<CGuarded *>(static_cast<CPhaseActor *>(this))));
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'pEntry' refers into the data of the actor and is used after a suspension; 'mp_Items' loses entries only in Destroy, so a guard of that phase that is alive at the use, like f_CheckDestroyedOnResume, makes it safe [malterlib-coroutine-actor-reference]
	}

	TCFuture<int> f_CalledDynamicCastMoved(bool _bKeep)
	{
		auto Guard = co_await (fg_Move(dynamic_cast<CGuarded *>(static_cast<CPhaseActor *>(this))))->f_Guard();
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'pEntry' refers into the data of the actor and is used after a suspension; 'mp_Items' loses entries only in Destroy, so a guard of that phase that is alive at the use, like f_CheckDestroyedOnResume, makes it safe [malterlib-coroutine-actor-reference]
	}

	TCFuture<int> f_ContextDynamicCastMoved(bool _bKeep)
	{
		auto Guard = co_await fs_GuardWithContext(this, fg_Move(dynamic_cast<CGuarded *>(static_cast<CPhaseActor *>(this))));
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_GuardedDynamicCastAddressOfDereferenced(bool _bKeep)
	{
		CGuarded *pMaybe = dynamic_cast<CGuarded *>(static_cast<CPhaseActor *>(this));
		auto Guard = co_await fs_Guard(&*pMaybe);
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'pEntry' refers into the data of the actor and is used after a suspension; 'mp_Items' loses entries only in Destroy, so a guard of that phase that is alive at the use, like f_CheckDestroyedOnResume, makes it safe [malterlib-coroutine-actor-reference]
	}

	TCFuture<int> f_CalledDynamicCastAddressOfDereferenced(bool _bKeep)
	{
		CGuarded *pMaybe = dynamic_cast<CGuarded *>(static_cast<CPhaseActor *>(this));
		auto Guard = co_await (&*pMaybe)->f_Guard();
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'pEntry' refers into the data of the actor and is used after a suspension; 'mp_Items' loses entries only in Destroy, so a guard of that phase that is alive at the use, like f_CheckDestroyedOnResume, makes it safe [malterlib-coroutine-actor-reference]
	}

	TCFuture<int> f_ContextDynamicCastAddressOfDereferenced(bool _bKeep)
	{
		CGuarded *pMaybe = dynamic_cast<CGuarded *>(static_cast<CPhaseActor *>(this));
		auto Guard = co_await fs_GuardWithContext(this, &*pMaybe);
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_GuardedDynamicUpcastDirect(bool _bKeep)
	{
		auto Guard = co_await fs_Guard(static_cast<CGuarded *>(dynamic_cast<CPhaseActor *>(this)));
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_CalledDynamicUpcastDirect(bool _bKeep)
	{
		auto Guard = co_await (static_cast<CGuarded *>(dynamic_cast<CPhaseActor *>(this)))->f_Guard();
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_ContextDynamicUpcastDirect(bool _bKeep)
	{
		auto Guard = co_await fs_GuardWithContext(this, static_cast<CGuarded *>(dynamic_cast<CPhaseActor *>(this)));
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_GuardedDynamicUpcastParenthesized(bool _bKeep)
	{
		auto Guard = co_await fs_Guard(((static_cast<CGuarded *>(dynamic_cast<CPhaseActor *>(this)))));
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_CalledDynamicUpcastParenthesized(bool _bKeep)
	{
		auto Guard = co_await (((static_cast<CGuarded *>(dynamic_cast<CPhaseActor *>(this)))))->f_Guard();
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_ContextDynamicUpcastParenthesized(bool _bKeep)
	{
		auto Guard = co_await fs_GuardWithContext(this, ((static_cast<CGuarded *>(dynamic_cast<CPhaseActor *>(this)))));
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_GuardedDynamicUpcastStaticCast(bool _bKeep)
	{
		auto Guard = co_await fs_Guard(static_cast<CGuarded *>(static_cast<CGuarded *>(dynamic_cast<CPhaseActor *>(this))));
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_CalledDynamicUpcastStaticCast(bool _bKeep)
	{
		auto Guard = co_await (static_cast<CGuarded *>(static_cast<CGuarded *>(dynamic_cast<CPhaseActor *>(this))))->f_Guard();
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_ContextDynamicUpcastStaticCast(bool _bKeep)
	{
		auto Guard = co_await fs_GuardWithContext(this, static_cast<CGuarded *>(static_cast<CGuarded *>(dynamic_cast<CPhaseActor *>(this))));
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_GuardedDynamicUpcastComma(bool _bKeep)
	{
		auto Guard = co_await fs_Guard(((void)0, static_cast<CGuarded *>(dynamic_cast<CPhaseActor *>(this))));
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_CalledDynamicUpcastComma(bool _bKeep)
	{
		auto Guard = co_await (((void)0, static_cast<CGuarded *>(dynamic_cast<CPhaseActor *>(this))))->f_Guard();
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_ContextDynamicUpcastComma(bool _bKeep)
	{
		auto Guard = co_await fs_GuardWithContext(this, ((void)0, static_cast<CGuarded *>(dynamic_cast<CPhaseActor *>(this))));
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_GuardedDynamicUpcastLocal(bool _bKeep)
	{
		CGuarded *pMaybe = static_cast<CGuarded *>(dynamic_cast<CPhaseActor *>(this));
		auto Guard = co_await fs_Guard(pMaybe);
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_CalledDynamicUpcastLocal(bool _bKeep)
	{
		CGuarded *pMaybe = static_cast<CGuarded *>(dynamic_cast<CPhaseActor *>(this));
		auto Guard = co_await (pMaybe)->f_Guard();
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_ContextDynamicUpcastLocal(bool _bKeep)
	{
		CGuarded *pMaybe = static_cast<CGuarded *>(dynamic_cast<CPhaseActor *>(this));
		auto Guard = co_await fs_GuardWithContext(this, pMaybe);
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_GuardedDynamicUpcastConstLocalReread(bool _bKeep)
	{
		CGuarded *const pMaybe = static_cast<CGuarded *>(dynamic_cast<CPhaseActor *>(this));
		auto Guard = co_await fs_Guard(*&pMaybe);
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_CalledDynamicUpcastConstLocalReread(bool _bKeep)
	{
		CGuarded *const pMaybe = static_cast<CGuarded *>(dynamic_cast<CPhaseActor *>(this));
		auto Guard = co_await (*&pMaybe)->f_Guard();
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_ContextDynamicUpcastConstLocalReread(bool _bKeep)
	{
		CGuarded *const pMaybe = static_cast<CGuarded *>(dynamic_cast<CPhaseActor *>(this));
		auto Guard = co_await fs_GuardWithContext(this, *&pMaybe);
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_GuardedDynamicUpcastReferenceAlias(bool _bKeep)
	{
		CGuarded *const &pMaybe = static_cast<CGuarded *>(dynamic_cast<CPhaseActor *>(this));
		auto Guard = co_await fs_Guard(pMaybe);
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_CalledDynamicUpcastReferenceAlias(bool _bKeep)
	{
		CGuarded *const &pMaybe = static_cast<CGuarded *>(dynamic_cast<CPhaseActor *>(this));
		auto Guard = co_await (pMaybe)->f_Guard();
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_ContextDynamicUpcastReferenceAlias(bool _bKeep)
	{
		CGuarded *const &pMaybe = static_cast<CGuarded *>(dynamic_cast<CPhaseActor *>(this));
		auto Guard = co_await fs_GuardWithContext(this, pMaybe);
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_GuardedDynamicUpcastIdentity(bool _bKeep)
	{
		auto Guard = co_await fs_Guard(fg_Identity(static_cast<CGuarded *>(dynamic_cast<CPhaseActor *>(this))));
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_CalledDynamicUpcastIdentity(bool _bKeep)
	{
		auto Guard = co_await (fg_Identity(static_cast<CGuarded *>(dynamic_cast<CPhaseActor *>(this))))->f_Guard();
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_ContextDynamicUpcastIdentity(bool _bKeep)
	{
		auto Guard = co_await fs_GuardWithContext(this, fg_Identity(static_cast<CGuarded *>(dynamic_cast<CPhaseActor *>(this))));
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_GuardedDynamicUpcastMoved(bool _bKeep)
	{
		auto Guard = co_await fs_Guard(fg_Move(static_cast<CGuarded *>(dynamic_cast<CPhaseActor *>(this))));
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_CalledDynamicUpcastMoved(bool _bKeep)
	{
		auto Guard = co_await (fg_Move(static_cast<CGuarded *>(dynamic_cast<CPhaseActor *>(this))))->f_Guard();
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_ContextDynamicUpcastMoved(bool _bKeep)
	{
		auto Guard = co_await fs_GuardWithContext(this, fg_Move(static_cast<CGuarded *>(dynamic_cast<CPhaseActor *>(this))));
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_GuardedDynamicUpcastAddressOfDereferenced(bool _bKeep)
	{
		CGuarded *pMaybe = static_cast<CGuarded *>(dynamic_cast<CPhaseActor *>(this));
		auto Guard = co_await fs_Guard(&*pMaybe);
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_CalledDynamicUpcastAddressOfDereferenced(bool _bKeep)
	{
		CGuarded *pMaybe = static_cast<CGuarded *>(dynamic_cast<CPhaseActor *>(this));
		auto Guard = co_await (&*pMaybe)->f_Guard();
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_ContextDynamicUpcastAddressOfDereferenced(bool _bKeep)
	{
		CGuarded *pMaybe = static_cast<CGuarded *>(dynamic_cast<CPhaseActor *>(this));
		auto Guard = co_await fs_GuardWithContext(this, &*pMaybe);
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_GuardedAccessorDirect(bool _bKeep)
	{
		auto Guard = co_await fs_Guard(f_Self());
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_CalledAccessorDirect(bool _bKeep)
	{
		auto Guard = co_await (f_Self())->f_Guard();
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_ContextAccessorDirect(bool _bKeep)
	{
		auto Guard = co_await fs_GuardWithContext(this, f_Self());
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_GuardedAccessorParenthesized(bool _bKeep)
	{
		auto Guard = co_await fs_Guard(((f_Self())));
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_CalledAccessorParenthesized(bool _bKeep)
	{
		auto Guard = co_await (((f_Self())))->f_Guard();
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_ContextAccessorParenthesized(bool _bKeep)
	{
		auto Guard = co_await fs_GuardWithContext(this, ((f_Self())));
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_GuardedAccessorStaticCast(bool _bKeep)
	{
		auto Guard = co_await fs_Guard(static_cast<CGuarded *>(f_Self()));
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_CalledAccessorStaticCast(bool _bKeep)
	{
		auto Guard = co_await (static_cast<CGuarded *>(f_Self()))->f_Guard();
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_ContextAccessorStaticCast(bool _bKeep)
	{
		auto Guard = co_await fs_GuardWithContext(this, static_cast<CGuarded *>(f_Self()));
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_GuardedAccessorComma(bool _bKeep)
	{
		auto Guard = co_await fs_Guard(((void)0, f_Self()));
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_CalledAccessorComma(bool _bKeep)
	{
		auto Guard = co_await (((void)0, f_Self()))->f_Guard();
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_ContextAccessorComma(bool _bKeep)
	{
		auto Guard = co_await fs_GuardWithContext(this, ((void)0, f_Self()));
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_GuardedAccessorLocal(bool _bKeep)
	{
		CGuarded *pMaybe = f_Self();
		auto Guard = co_await fs_Guard(pMaybe);
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_CalledAccessorLocal(bool _bKeep)
	{
		CGuarded *pMaybe = f_Self();
		auto Guard = co_await (pMaybe)->f_Guard();
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_ContextAccessorLocal(bool _bKeep)
	{
		CGuarded *pMaybe = f_Self();
		auto Guard = co_await fs_GuardWithContext(this, pMaybe);
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_GuardedAccessorConstLocalReread(bool _bKeep)
	{
		CGuarded *const pMaybe = f_Self();
		auto Guard = co_await fs_Guard(*&pMaybe);
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_CalledAccessorConstLocalReread(bool _bKeep)
	{
		CGuarded *const pMaybe = f_Self();
		auto Guard = co_await (*&pMaybe)->f_Guard();
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_ContextAccessorConstLocalReread(bool _bKeep)
	{
		CGuarded *const pMaybe = f_Self();
		auto Guard = co_await fs_GuardWithContext(this, *&pMaybe);
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_GuardedAccessorReferenceAlias(bool _bKeep)
	{
		CGuarded *const &pMaybe = f_Self();
		auto Guard = co_await fs_Guard(pMaybe);
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_CalledAccessorReferenceAlias(bool _bKeep)
	{
		CGuarded *const &pMaybe = f_Self();
		auto Guard = co_await (pMaybe)->f_Guard();
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_ContextAccessorReferenceAlias(bool _bKeep)
	{
		CGuarded *const &pMaybe = f_Self();
		auto Guard = co_await fs_GuardWithContext(this, pMaybe);
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_GuardedAccessorIdentity(bool _bKeep)
	{
		auto Guard = co_await fs_Guard(fg_Identity(f_Self()));
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_CalledAccessorIdentity(bool _bKeep)
	{
		auto Guard = co_await (fg_Identity(f_Self()))->f_Guard();
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_ContextAccessorIdentity(bool _bKeep)
	{
		auto Guard = co_await fs_GuardWithContext(this, fg_Identity(f_Self()));
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_GuardedAccessorMoved(bool _bKeep)
	{
		auto Guard = co_await fs_Guard(fg_Move(f_Self()));
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_CalledAccessorMoved(bool _bKeep)
	{
		auto Guard = co_await (fg_Move(f_Self()))->f_Guard();
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_ContextAccessorMoved(bool _bKeep)
	{
		auto Guard = co_await fs_GuardWithContext(this, fg_Move(f_Self()));
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_GuardedAccessorAddressOfDereferenced(bool _bKeep)
	{
		CGuarded *pMaybe = f_Self();
		auto Guard = co_await fs_Guard(&*pMaybe);
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_CalledAccessorAddressOfDereferenced(bool _bKeep)
	{
		CGuarded *pMaybe = f_Self();
		auto Guard = co_await (&*pMaybe)->f_Guard();
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_ContextAccessorAddressOfDereferenced(bool _bKeep)
	{
		CGuarded *pMaybe = f_Self();
		auto Guard = co_await fs_GuardWithContext(this, &*pMaybe);
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_GuardedNullAccessorDirect(bool _bKeep)
	{
		auto Guard = co_await fs_Guard(f_MaybeSelf(_bKeep));
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'pEntry' refers into the data of the actor and is used after a suspension; 'mp_Items' loses entries only in Destroy, so a guard of that phase that is alive at the use, like f_CheckDestroyedOnResume, makes it safe [malterlib-coroutine-actor-reference]
	}

	TCFuture<int> f_CalledNullAccessorDirect(bool _bKeep)
	{
		auto Guard = co_await (f_MaybeSelf(_bKeep))->f_Guard();
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'pEntry' refers into the data of the actor and is used after a suspension; 'mp_Items' loses entries only in Destroy, so a guard of that phase that is alive at the use, like f_CheckDestroyedOnResume, makes it safe [malterlib-coroutine-actor-reference]
	}

	TCFuture<int> f_ContextNullAccessorDirect(bool _bKeep)
	{
		auto Guard = co_await fs_GuardWithContext(this, f_MaybeSelf(_bKeep));
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_GuardedNullAccessorParenthesized(bool _bKeep)
	{
		auto Guard = co_await fs_Guard(((f_MaybeSelf(_bKeep))));
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'pEntry' refers into the data of the actor and is used after a suspension; 'mp_Items' loses entries only in Destroy, so a guard of that phase that is alive at the use, like f_CheckDestroyedOnResume, makes it safe [malterlib-coroutine-actor-reference]
	}

	TCFuture<int> f_CalledNullAccessorParenthesized(bool _bKeep)
	{
		auto Guard = co_await (((f_MaybeSelf(_bKeep))))->f_Guard();
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'pEntry' refers into the data of the actor and is used after a suspension; 'mp_Items' loses entries only in Destroy, so a guard of that phase that is alive at the use, like f_CheckDestroyedOnResume, makes it safe [malterlib-coroutine-actor-reference]
	}

	TCFuture<int> f_ContextNullAccessorParenthesized(bool _bKeep)
	{
		auto Guard = co_await fs_GuardWithContext(this, ((f_MaybeSelf(_bKeep))));
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_GuardedNullAccessorStaticCast(bool _bKeep)
	{
		auto Guard = co_await fs_Guard(static_cast<CGuarded *>(f_MaybeSelf(_bKeep)));
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'pEntry' refers into the data of the actor and is used after a suspension; 'mp_Items' loses entries only in Destroy, so a guard of that phase that is alive at the use, like f_CheckDestroyedOnResume, makes it safe [malterlib-coroutine-actor-reference]
	}

	TCFuture<int> f_CalledNullAccessorStaticCast(bool _bKeep)
	{
		auto Guard = co_await (static_cast<CGuarded *>(f_MaybeSelf(_bKeep)))->f_Guard();
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'pEntry' refers into the data of the actor and is used after a suspension; 'mp_Items' loses entries only in Destroy, so a guard of that phase that is alive at the use, like f_CheckDestroyedOnResume, makes it safe [malterlib-coroutine-actor-reference]
	}

	TCFuture<int> f_ContextNullAccessorStaticCast(bool _bKeep)
	{
		auto Guard = co_await fs_GuardWithContext(this, static_cast<CGuarded *>(f_MaybeSelf(_bKeep)));
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_GuardedNullAccessorComma(bool _bKeep)
	{
		auto Guard = co_await fs_Guard(((void)0, f_MaybeSelf(_bKeep)));
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'pEntry' refers into the data of the actor and is used after a suspension; 'mp_Items' loses entries only in Destroy, so a guard of that phase that is alive at the use, like f_CheckDestroyedOnResume, makes it safe [malterlib-coroutine-actor-reference]
	}

	TCFuture<int> f_CalledNullAccessorComma(bool _bKeep)
	{
		auto Guard = co_await (((void)0, f_MaybeSelf(_bKeep)))->f_Guard();
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'pEntry' refers into the data of the actor and is used after a suspension; 'mp_Items' loses entries only in Destroy, so a guard of that phase that is alive at the use, like f_CheckDestroyedOnResume, makes it safe [malterlib-coroutine-actor-reference]
	}

	TCFuture<int> f_ContextNullAccessorComma(bool _bKeep)
	{
		auto Guard = co_await fs_GuardWithContext(this, ((void)0, f_MaybeSelf(_bKeep)));
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_GuardedNullAccessorLocal(bool _bKeep)
	{
		CGuarded *pMaybe = f_MaybeSelf(_bKeep);
		auto Guard = co_await fs_Guard(pMaybe);
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'pEntry' refers into the data of the actor and is used after a suspension; 'mp_Items' loses entries only in Destroy, so a guard of that phase that is alive at the use, like f_CheckDestroyedOnResume, makes it safe [malterlib-coroutine-actor-reference]
	}

	TCFuture<int> f_CalledNullAccessorLocal(bool _bKeep)
	{
		CGuarded *pMaybe = f_MaybeSelf(_bKeep);
		auto Guard = co_await (pMaybe)->f_Guard();
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'pEntry' refers into the data of the actor and is used after a suspension; 'mp_Items' loses entries only in Destroy, so a guard of that phase that is alive at the use, like f_CheckDestroyedOnResume, makes it safe [malterlib-coroutine-actor-reference]
	}

	TCFuture<int> f_ContextNullAccessorLocal(bool _bKeep)
	{
		CGuarded *pMaybe = f_MaybeSelf(_bKeep);
		auto Guard = co_await fs_GuardWithContext(this, pMaybe);
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_GuardedNullAccessorConstLocalReread(bool _bKeep)
	{
		CGuarded *const pMaybe = f_MaybeSelf(_bKeep);
		auto Guard = co_await fs_Guard(*&pMaybe);
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'pEntry' refers into the data of the actor and is used after a suspension; 'mp_Items' loses entries only in Destroy, so a guard of that phase that is alive at the use, like f_CheckDestroyedOnResume, makes it safe [malterlib-coroutine-actor-reference]
	}

	TCFuture<int> f_CalledNullAccessorConstLocalReread(bool _bKeep)
	{
		CGuarded *const pMaybe = f_MaybeSelf(_bKeep);
		auto Guard = co_await (*&pMaybe)->f_Guard();
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'pEntry' refers into the data of the actor and is used after a suspension; 'mp_Items' loses entries only in Destroy, so a guard of that phase that is alive at the use, like f_CheckDestroyedOnResume, makes it safe [malterlib-coroutine-actor-reference]
	}

	TCFuture<int> f_ContextNullAccessorConstLocalReread(bool _bKeep)
	{
		CGuarded *const pMaybe = f_MaybeSelf(_bKeep);
		auto Guard = co_await fs_GuardWithContext(this, *&pMaybe);
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_GuardedNullAccessorReferenceAlias(bool _bKeep)
	{
		CGuarded *const &pMaybe = f_MaybeSelf(_bKeep);
		auto Guard = co_await fs_Guard(pMaybe);
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'pEntry' refers into the data of the actor and is used after a suspension; 'mp_Items' loses entries only in Destroy, so a guard of that phase that is alive at the use, like f_CheckDestroyedOnResume, makes it safe [malterlib-coroutine-actor-reference]
	}

	TCFuture<int> f_CalledNullAccessorReferenceAlias(bool _bKeep)
	{
		CGuarded *const &pMaybe = f_MaybeSelf(_bKeep);
		auto Guard = co_await (pMaybe)->f_Guard();
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'pEntry' refers into the data of the actor and is used after a suspension; 'mp_Items' loses entries only in Destroy, so a guard of that phase that is alive at the use, like f_CheckDestroyedOnResume, makes it safe [malterlib-coroutine-actor-reference]
	}

	TCFuture<int> f_ContextNullAccessorReferenceAlias(bool _bKeep)
	{
		CGuarded *const &pMaybe = f_MaybeSelf(_bKeep);
		auto Guard = co_await fs_GuardWithContext(this, pMaybe);
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_GuardedNullAccessorIdentity(bool _bKeep)
	{
		auto Guard = co_await fs_Guard(fg_Identity(f_MaybeSelf(_bKeep)));
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'pEntry' refers into the data of the actor and is used after a suspension; 'mp_Items' loses entries only in Destroy, so a guard of that phase that is alive at the use, like f_CheckDestroyedOnResume, makes it safe [malterlib-coroutine-actor-reference]
	}

	TCFuture<int> f_CalledNullAccessorIdentity(bool _bKeep)
	{
		auto Guard = co_await (fg_Identity(f_MaybeSelf(_bKeep)))->f_Guard();
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'pEntry' refers into the data of the actor and is used after a suspension; 'mp_Items' loses entries only in Destroy, so a guard of that phase that is alive at the use, like f_CheckDestroyedOnResume, makes it safe [malterlib-coroutine-actor-reference]
	}

	TCFuture<int> f_ContextNullAccessorIdentity(bool _bKeep)
	{
		auto Guard = co_await fs_GuardWithContext(this, fg_Identity(f_MaybeSelf(_bKeep)));
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_GuardedNullAccessorMoved(bool _bKeep)
	{
		auto Guard = co_await fs_Guard(fg_Move(f_MaybeSelf(_bKeep)));
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'pEntry' refers into the data of the actor and is used after a suspension; 'mp_Items' loses entries only in Destroy, so a guard of that phase that is alive at the use, like f_CheckDestroyedOnResume, makes it safe [malterlib-coroutine-actor-reference]
	}

	TCFuture<int> f_CalledNullAccessorMoved(bool _bKeep)
	{
		auto Guard = co_await (fg_Move(f_MaybeSelf(_bKeep)))->f_Guard();
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'pEntry' refers into the data of the actor and is used after a suspension; 'mp_Items' loses entries only in Destroy, so a guard of that phase that is alive at the use, like f_CheckDestroyedOnResume, makes it safe [malterlib-coroutine-actor-reference]
	}

	TCFuture<int> f_ContextNullAccessorMoved(bool _bKeep)
	{
		auto Guard = co_await fs_GuardWithContext(this, fg_Move(f_MaybeSelf(_bKeep)));
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_GuardedNullAccessorAddressOfDereferenced(bool _bKeep)
	{
		CGuarded *pMaybe = f_MaybeSelf(_bKeep);
		auto Guard = co_await fs_Guard(&*pMaybe);
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'pEntry' refers into the data of the actor and is used after a suspension; 'mp_Items' loses entries only in Destroy, so a guard of that phase that is alive at the use, like f_CheckDestroyedOnResume, makes it safe [malterlib-coroutine-actor-reference]
	}

	TCFuture<int> f_CalledNullAccessorAddressOfDereferenced(bool _bKeep)
	{
		CGuarded *pMaybe = f_MaybeSelf(_bKeep);
		auto Guard = co_await (&*pMaybe)->f_Guard();
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'pEntry' refers into the data of the actor and is used after a suspension; 'mp_Items' loses entries only in Destroy, so a guard of that phase that is alive at the use, like f_CheckDestroyedOnResume, makes it safe [malterlib-coroutine-actor-reference]
	}

	TCFuture<int> f_ContextNullAccessorAddressOfDereferenced(bool _bKeep)
	{
		CGuarded *pMaybe = f_MaybeSelf(_bKeep);
		auto Guard = co_await fs_GuardWithContext(this, &*pMaybe);
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_OtherMessageDefault(bool _bKeep)
	{
		auto Guard = co_await f_Guard();
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_OtherMessageNull(bool _bKeep)
	{
		auto Guard = co_await f_Guard(nullptr);
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_OtherMessageMaybe(bool _bKeep)
	{
		auto Guard = co_await f_Guard(_bKeep ? "Stopping" : nullptr);
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_OtherContextNull(bool _bKeep)
	{
		auto Guard = co_await fs_GuardWithContext(this, nullptr);
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_OtherMemberMaybe(bool _bKeep)
	{
		auto Guard = co_await fs_GuardWithMember(this, _bKeep ? &mp_Items : nullptr);
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	TCFuture<int> f_OtherMemberNull(bool _bKeep)
	{
		auto Guard = co_await fs_GuardWithMember(this, nullptr);
		auto *pEntry = mp_Items.f_FindEqual("Key");
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

};
