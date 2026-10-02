#pragma once

#include "malterlib-concurrency.h"

// The annotations of Mib/Concurrency/TidyAnnotations.
#define DMibActorEntries(...) [[clang::annotate("malterlib-actor-entries", #__VA_ARGS__)]]
#define DMibActorPhase(...) [[clang::annotate("malterlib-actor-phase", #__VA_ARGS__)]]
#define DMibActorPhaseEntry(...) [[clang::annotate("malterlib-actor-phase-entry", #__VA_ARGS__)]]
#define DMibActorPhaseFlag(...) [[clang::annotate("malterlib-actor-phase-flag", #__VA_ARGS__)]]
#define DMibActorPhaseGuard(...) [[clang::annotate("malterlib-actor-phase-guard", #__VA_ARGS__)]]

namespace NMib::NContainer
{
	template <typename t_CKey, typename t_CValue>
	struct TCMap
	{
		struct CIterator
		{
			explicit operator bool () const;
			t_CValue &operator * () const;
			t_CValue *operator -> () const;
			CIterator &operator ++ ();
			void f_Remove();
			CIterator f_GetIterator() const;
			void f_Next();
			void f_Prev();
		};

		t_CValue &operator [] (t_CKey const &_Key);
		t_CValue *f_FindEqual(t_CKey const &_Key);
		t_CValue const *f_FindEqual(t_CKey const &_Key) const;
		bool f_Remove(t_CKey const &_Key);
		// Takes the key by a forwarding reference, like TCMap.
		template <typename tf_CKey>
		bool f_RemoveKey(tf_CKey &&_Key);
		void f_Clear();
		bool f_TryRemovePointerBasedComparison(t_CValue *_pValue);
		template <typename tf_CStream>
		void f_Consume(tf_CStream &_Stream);
		CIterator f_GetIterator();
		CIterator f_GetIterator(t_CKey const &_Key);
		t_CValue *begin();
		t_CValue *end();
	};

	// A container a loop goes over through free functions, like Malterlib's.
	template <typename t_CValue>
	struct TCFreeList
	{
		t_CValue *f_GetFirst();
		void f_Clear();
	};

	template <typename tf_CContainer>
	auto begin(tf_CContainer &&_Container) -> decltype(_Container.f_GetFirst());
	template <typename tf_CContainer>
	auto end(tf_CContainer &&_Container) -> decltype(_Container.f_GetFirst());

	template <typename t_CValue>
	struct TCLinkedList
	{
		t_CValue *f_GetFirst();
		void f_Clear();
	};

	template <typename t_CValue>
	struct TCVector
	{
		t_CValue &operator [] (int _Index);
		t_CValue &f_Insert(t_CValue const &_Value);
		void f_Clear();
	};
}

namespace NMib::NStr
{
	struct CStr
	{
		CStr();
		CStr(char const *);
		bool operator < (CStr const &) const;
	};
}

namespace NMib::NCryptography
{
	template <typename t_CContainer>
	NStr::CStr fg_RandomID(t_CContainer &_Container);
}

namespace NMib::NConcurrency
{
	struct CExceptionPointer
	{
		CExceptionPointer();
		CExceptionPointer(decltype(nullptr));
	};

	CExceptionPointer fg_MakeError(char const *_pMessage);

	// A result that, once set, ends the coroutine like an error does.
	template <typename t_CType>
	struct TCAsyncResult
	{
		using CValue = t_CType;

		void f_SetResult(t_CType _Value);
		void f_SetResult(TCAsyncResult const &_Other);
		void f_SetException(CExceptionPointer _pException);
		void f_Clear();
	};

	// An error converted to the pointer, like `DMibErrorInstance(...)` gives.
	struct CException
	{
		CException(char const *_pType, char const *_pMessage);
		operator CExceptionPointer() const;
	};

	// Ends the coroutine on resume once the guarded phase began, while it lives.
	struct COnResumeScope
	{
		~COnResumeScope();
	};

	// The callback gives an exception pointer, or a result with a value type,
	// like the real one requires.
	template <typename t_CResult>
	concept COnResumeResult = requires { typename t_CResult::CValue; };

	template <typename t_FFunction>
		requires requires (t_FFunction &_fFunction)
		{
			requires COnResumeResult<decltype(_fFunction())> || __is_same(decltype(_fFunction()), CExceptionPointer);
		}
	TCFuture<COnResumeScope> fg_OnResume(t_FFunction &&_fFunction)
	{
		return {};
	}

	struct CPhaseActor : CActor
	{
		DMibActorPhaseFlag(Destroy) bool f_IsDestroyed() const;
		DMibActorPhaseGuard(Destroy) TCFuture<COnResumeScope> f_CheckDestroyedOnResume() const;

		DMibActorPhase(Destroy) virtual TCFuture<int> fp_Destroy();
		DMibActorPhaseEntry(Destroy) TCFuture<int> fp_DestroyInternal()
		{
			co_await fp_Destroy();
			co_return 0;
		}
	};

	struct CDistributedAppActor : CPhaseActor
	{
		struct CState
		{
			DMibActorPhaseFlag(StopApp) bool m_bStoppingApp = false;
		};

		DMibActorPhaseGuard(Destroy, StopApp) TCFuture<COnResumeScope> fp_CheckStoppedOrDestroyedOnResume() const;

		DMibActorPhase(StopApp) virtual TCFuture<int> fp_StopApp();
		DMibActorPhaseEntry(StopApp) TCFuture<int> f_StopApp()
		{
			mp_State.m_bStoppingApp = true;
			co_await fp_StopApp();
			co_return 0;
		}

		CState mp_State;
	};
}

using namespace NMib::NContainer;
using namespace NMib::NStr;
using namespace NMib::NCryptography;
