// RUN: %check_clang_tidy -std=c++20-or-later %s malterlib-coroutine-actor-reference %t -- -config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" -- -I %S/Inputs

#include "malterlib-concurrency.h"

struct CValue
{
	int m_Value = 0;
};

struct CMap
{
	CValue *f_FindEqual(int _Key);
	CValue &operator [] (int _Key);
	// Finds or adds an entry, like `TCMap`.
	CValue *operator () (int _Key);
};

// The registration ends when its result is destroyed.
struct COnResume
{
	~COnResume();
};

// The callback gives an exception pointer, like the real one requires.
struct CExceptionPointer
{
	CExceptionPointer();
	CExceptionPointer(decltype(nullptr));
};

// The exception that ends the coroutine on resume.
CExceptionPointer fg_Error();

template <typename t_FFunction>
	requires requires (t_FFunction &_fFunction)
	{
		requires __is_same(decltype(_fFunction()), CExceptionPointer);
	}
TCFuture<COnResume> fg_OnResume(t_FFunction &&_fFunction)
{
	return {};
}

TCFuture<int> fg_Suspend();
void fg_Read(CValue const *_pValue);
void fg_Throw();

struct CInternal
{
	CMap m_Items;
};

struct CMyActor : CActor
{
	TCFuture<int> f_Stale(int _Key)
	{
		CValue *pValue = mp_Items.f_FindEqual(_Key);
		CValue &Value = mp_Items[_Key];
		int Before = pValue->m_Value + Value.m_Value;

		co_await fg_Suspend();

		int After = pValue->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pValue' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
		After += Value.m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:12: warning: 'Value' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]

		co_return Before + After;
	}

	TCFuture<int> f_FindOrAdd(int _Key)
	{
		auto &Value = *mp_Items(_Key);
		co_await fg_Suspend();
		co_return Value.m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'Value' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
	}

	TCFuture<int> f_WriteThroughReference(int _Key)
	{
		CValue &Value = mp_Items[_Key];
		co_await fg_Suspend();
		Value = CValue{};
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: 'Value' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
		co_return 0;
	}

	template <typename tf_CFuture>
	TCFuture<int> f_Dependent(tf_CFuture _Future)
	{
		CValue *pValue = mp_Items.f_FindEqual(0);
		co_await fg_Move(_Future);
		co_return pValue->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'pValue' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
	}

	TCFuture<int> f_LookedUpAgain(int _Key)
	{
		CValue *pValue = mp_Items.f_FindEqual(_Key);
		co_await fg_Suspend();
		pValue = mp_Items.f_FindEqual(_Key);
		co_return pValue->m_Value;
	}

	TCFuture<int> f_Revalidated(int _Key)
	{
		CValue *pValue = nullptr;
		auto OnResume = co_await fg_OnResume
			(
				[&] -> CExceptionPointer
				{
					pValue = mp_Items.f_FindEqual(_Key);
					return {};
				}
			)
		;
		pValue = mp_Items.f_FindEqual(_Key);
		co_await fg_Suspend();
		co_return pValue->m_Value;
	}

	TCFuture<int> f_PointerReference(int _Key)
	{
		// A reference to the pointer sees it looked up again.
		CValue *pValue = mp_Items.f_FindEqual(_Key);
		CValue *&pAlias = pValue;
		co_await fg_Suspend();
		pValue = mp_Items.f_FindEqual(_Key);
		co_return pAlias->m_Value;
	}

	TCFuture<int> f_InvokedLambda(int _Key)
	{
		CValue *pValue = mp_Items.f_FindEqual(_Key);
		co_await fg_Suspend();
		co_return [pValue] { return pValue->m_Value; }();
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: 'pValue' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
	}

	TCFuture<int> f_NamedInvokedLambda(int _Key)
	{
		CValue *pValue = mp_Items.f_FindEqual(_Key);
		auto fRead = [pValue] { return pValue->m_Value; };
		co_await fg_Suspend();
		co_return fRead();
// CHECK-MESSAGES: :[[@LINE-3]]:17: warning: 'pValue' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
	}

	TCFuture<int> f_OtherStorage(int _Key)
	{
		// A pointer assigned storage of its own doesn't go stale.
		CValue *pValue = mp_Items.f_FindEqual(_Key);
		CValue Local;
		pValue = &Local;
		co_await fg_Suspend();
		co_return pValue->m_Value;
	}

	TCFuture<int> f_CopiedLocal(int _Key)
	{
		// A copy of a pointer that points to a local of the coroutine.
		CValue *pValue = mp_Items.f_FindEqual(_Key);
		CValue Local;
		pValue = &Local;
		CValue *pCopy = pValue;
		co_await fg_Suspend();
		co_return pCopy->m_Value;
	}

	TCFuture<int> f_StaleCopyInLambda(int _Key)
	{
		// The lambda's copy is not refreshed by looking the pointer up again.
		CValue *pValue = mp_Items.f_FindEqual(_Key);
		auto fRead = [pValue] { return pValue->m_Value; };
		co_await fg_Suspend();
		pValue = mp_Items.f_FindEqual(_Key);
		co_return fRead() + pValue->m_Value;
// CHECK-MESSAGES: :[[@LINE-4]]:17: warning: 'pValue' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
	}

	TCFuture<int> f_AssignedBeforeCapture(int _Key)
	{
		CValue *pValue = nullptr;
		pValue = mp_Items.f_FindEqual(_Key);
		auto fLambda = [this, pValue]() -> TCFuture<int>
			{
				co_await fg_Suspend();
				co_return pValue->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pValue' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
			}
		;
		co_return 0;
	}

	TCFuture<int> f_LocalWhenCaptured(int _Key)
	{
		// The pointer points to a local when the lambda copies it.
		CValue *pValue = mp_Items.f_FindEqual(_Key);
		CValue Local;
		pValue = &Local;
		auto fLambda = [this, pValue]() -> TCFuture<int>
			{
				co_await fg_Suspend();
				co_return pValue->m_Value;
			}
		;
		co_return 0;
	}

	TCFuture<int> f_ParenthesizedCopy(int _Key)
	{
		CValue *pValue = mp_Items.f_FindEqual(_Key);
		auto fRead = [pCopy = (pValue)] { return pCopy->m_Value; };
		co_await fg_Suspend();
		pValue = mp_Items.f_FindEqual(_Key);
		co_return fRead() + pValue->m_Value;
// CHECK-MESSAGES: :[[@LINE-4]]:17: warning: 'pCopy' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
	}

	TCFuture<int> f_ReferenceToLocal(int _Key)
	{
		// A reference bound through a pointer that points to a local.
		CValue *pValue = mp_Items.f_FindEqual(_Key);
		CValue Local;
		pValue = &Local;
		CValue &Alias = *pValue;
		co_await fg_Suspend();
		co_return Alias.m_Value;
	}

	TCFuture<int> f_StaleIntoCoroutine(int _Key)
	{
		// A stale pointer copied into a coroutine lambda.
		CValue *pValue = mp_Items.f_FindEqual(_Key);
		co_await fg_Suspend();
		co_return co_await fg_CallSafe
			(
				[pValue]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:6: warning: 'pValue' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
				{
					co_return pValue->m_Value;
				}
			)
		;
	}

	TCFuture<int> f_InitCaptureLookup(int _Key)
	{
		auto fRead = [pValue = mp_Items.f_FindEqual(_Key)] { return pValue->m_Value; };
		co_await fg_Suspend();
		co_return fRead();
// CHECK-MESSAGES: :[[@LINE-3]]:17: warning: 'pValue' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
	}

	TCFuture<int> f_LoopCarriedAlias(int _Key)
	{
		CValue *pFirst = nullptr;
		CValue *pSecond = nullptr;
		for (int iTry = 0; iTry < 2; ++iTry)
		{
			pSecond = pFirst;
			pFirst = mp_Items.f_FindEqual(_Key);
		}
		co_await fg_Suspend();
		co_return pSecond ? pSecond->m_Value : 0;
// CHECK-MESSAGES: :[[@LINE-1]]:23: warning: 'pSecond' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
	}

	TCFuture<int> f_RevalidatedToLocal(int _Key)
	{
		// The callback points the pointer at a local of the coroutine.
		CValue Local;
		CValue *pValue = mp_Items.f_FindEqual(_Key);
		{
			auto OnResume = co_await fg_OnResume
				(
					[&] -> CExceptionPointer
					{
						pValue = &Local;
						return {};
					}
				)
			;
		}
		co_await fg_Suspend();
		co_return pValue->m_Value;
	}

	TCFuture<int> f_RevalidatedToLocalOnOnePath(int _Key, bool _bRedirect)
	{
		// A path of the callback that doesn't assign the pointer leaves it in
		// the data.
		CValue Local;
		CValue *pValue = mp_Items.f_FindEqual(_Key);
		{
			auto OnResume = co_await fg_OnResume
				(
					[&] -> CExceptionPointer
					{
						if (_bRedirect)
							pValue = &Local;
						return {};
					}
				)
			;
		}
		co_await fg_Suspend();
		co_return pValue->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'pValue' refers into the data of the actor and is used after a suspension, {{.*}}
	}

	TCFuture<int> f_RedirectedContainer()
	{
		// A lookup in a container the pointer no longer points into the actor.
		CMap LocalItems;
		CMap *pItems = &mp_Items;
		pItems = &LocalItems;
		CValue *pValue = pItems->f_FindEqual(0);
		co_await fg_Suspend();
		co_return pValue->m_Value;
	}

	TCFuture<int> f_RevalidatedOnOnePath(int _Key, bool _bLocal)
	{
		// The callback looks the data up on one path, so it may point into it.
		CValue Local;
		CValue *pValue = mp_Items.f_FindEqual(_Key);
		{
			auto OnResume = co_await fg_OnResume
				(
					[&] -> CExceptionPointer
					{
						if (_bLocal)
							pValue = mp_Items.f_FindEqual(_Key);
						else
							pValue = &Local;
						return {};
					}
				)
			;
		}
		co_await fg_Suspend();
		co_return pValue->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'pValue' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
	}

	TCFuture<int> f_WriteThroughNotRevalidation(int _Key)
	{
		// Assigning through a reference writes the data; it doesn't look it up.
		CValue &Value = mp_Items[_Key];
		auto OnResume = co_await fg_OnResume
			(
				[&] -> CExceptionPointer
				{
					Value = mp_Items[_Key];
// CHECK-MESSAGES: :[[@LINE-1]]:6: warning: 'Value' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
					return {};
				}
			)
		;
		co_await fg_Suspend();
		co_return Value.m_Value;
	}

	TCFuture<int> f_OnlyRead(int _Key)
	{
		// A callback that only reads the pointer doesn't re-validate it.
		CValue *pValue = mp_Items.f_FindEqual(_Key);
		auto OnResume = co_await fg_OnResume
			(
				[&] -> CExceptionPointer
				{
					fg_Read(pValue);
					return {};
				}
			)
		;
		co_await fg_Suspend();
		co_return pValue->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'pValue' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
	}

	TCFuture<int> f_Validated(int _Key)
	{
		// A callback that returns something, like the exception that ends the
		// coroutine when the data is gone, validates what it reads.
		CValue &Value = mp_Items[_Key];
		auto OnResume = co_await fg_OnResume
			(
				[&]() -> CExceptionPointer
				{
					if (mp_Items.f_FindEqual(_Key) != &Value)
						return fg_Error();
					return {};
				}
			)
		;
		co_await fg_Suspend();
		co_return Value.m_Value;
	}

	TCFuture<int> f_RevalidatedNamed(int _Key)
	{
		CValue *pValue = mp_Items.f_FindEqual(_Key);
		auto fRevalidate = [&] -> CExceptionPointer
			{
				pValue = mp_Items.f_FindEqual(_Key);
				return {};
			}
		;
		auto OnResume = co_await fg_OnResume(fRevalidate);
		co_await fg_Suspend();
		co_return pValue->m_Value;
	}

	TCFuture<int> f_ThrownAfterSuspension(int _Key)
	{
		CValue *pValue = mp_Items.f_FindEqual(_Key);
		try
		{
			co_await CThrowingAwaiter{};
		}
		catch (...)
		{
			co_return pValue->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: 'pValue' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
		}
		co_return 0;
	}

	TCFuture<int> f_RevalidatedReferenceCapture(int _Key)
	{
		CValue *pValue = mp_Items.f_FindEqual(_Key);
		auto OnResume = co_await fg_OnResume
			(
				[&pCurrent = pValue, this, _Key] -> CExceptionPointer
				{
					pCurrent = mp_Items.f_FindEqual(_Key);
					return {};
				}
			)
		;
		co_await fg_Suspend();
		co_return pValue->m_Value;
	}

	TCFuture<int> f_RevalidatedCopy(int _Key)
	{
		CValue *pValue = mp_Items.f_FindEqual(_Key);
		auto OnResume = co_await fg_OnResume
			(
				[this, pValue, _Key]() mutable -> CExceptionPointer
				{
					pValue = mp_Items.f_FindEqual(_Key);
					return {};
				}
			)
		;
		co_await fg_Suspend();
		co_return pValue->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'pValue' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
	}

	TCFuture<int> f_RevalidationNotReached(int _Key)
	{
		CValue *pValue = mp_Items.f_FindEqual(_Key);
		if (false)
		{
			auto OnResume = co_await fg_OnResume
				(
					[&] -> CExceptionPointer
					{
						pValue = mp_Items.f_FindEqual(_Key);
						return {};
					}
				)
			;
		}
		co_await fg_Suspend();
		co_return pValue->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'pValue' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
	}

	TCFuture<int> f_Handler(int _Key)
	{
		CValue *pValue = mp_Items.f_FindEqual(_Key);
		try
		{
			co_await fg_Suspend();
			fg_Throw();
		}
		catch (...)
		{
			co_return pValue->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: 'pValue' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
		}
		co_return 0;
	}

	TCFuture<int> f_RevalidationScope(int _Key)
	{
		CValue *pValue = mp_Items.f_FindEqual(_Key);
		{
			auto OnResume = co_await fg_OnResume
				(
					[&] -> CExceptionPointer
					{
						pValue = mp_Items.f_FindEqual(_Key);
						return {};
					}
				)
			;
			co_await fg_Suspend();
			int Inside = pValue->m_Value;
		}
		co_await fg_Suspend();
		co_return pValue->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'pValue' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
	}

	TCFuture<int> f_RevalidatedLater(int _Key)
	{
		CValue *pValue = mp_Items.f_FindEqual(_Key);
		co_await fg_Suspend();
		auto OnResume = co_await fg_OnResume
			(
				[&] -> CExceptionPointer
				{
					pValue = mp_Items.f_FindEqual(_Key);
					return {};
				}
			)
		;
		co_return pValue->m_Value;
	}

	TCFuture<int> f_Capture(int _Key)
	{
		CValue *pValue = mp_Items.f_FindEqual(_Key);
		auto fLambda = [this, pValue]() -> TCFuture<int>
			{
				co_await fg_Suspend();
				co_return pValue->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pValue' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
			}
		;
		co_return 0;
	}

	TCFuture<int> f_Overlapping(int _Key)
	{
		CValue *pValue = mp_Items.f_FindEqual(_Key);
		auto Outer = co_await fg_OnResume
			(
				[&] -> CExceptionPointer
				{
					pValue = mp_Items.f_FindEqual(_Key);
					return {};
				}
			)
		;
		{
			auto Inner = co_await fg_OnResume
				(
					[&] -> CExceptionPointer
					{
						pValue = mp_Items.f_FindEqual(_Key);
						return {};
					}
				)
			;
		}
		co_await fg_Suspend();
		co_return pValue->m_Value;
	}

	TCFuture<int> f_AssignedLookup(int _Key)
	{
		CValue *pValue = nullptr;
		pValue = mp_Items.f_FindEqual(_Key);
		co_await fg_Suspend();
		co_return pValue->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'pValue' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
	}

	TCFuture<int> f_Owned()
	{
		// The internal data lives as long as the actor.
		auto &Internal = *mp_pInternal;
		co_await fg_Suspend();
		co_return Internal.m_Items.f_FindEqual(0)->m_Value;
	}

	TCFuture<int> f_Local(int _Key)
	{
		CMap Local;
		CValue *pValue = Local.f_FindEqual(m_Key);
		co_await fg_Suspend();
		co_return pValue->m_Value;
	}

	TCFuture<int> f_Alias(int _Key)
	{
		auto &Items = mp_Items;
		CValue *pValue = Items.f_FindEqual(_Key);
		co_await fg_Suspend();
		co_return pValue->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'pValue' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
	}

	TCFuture<int> f_InitCapture(int _Key)
	{
		auto fLambda = [this, pValue = mp_Items.f_FindEqual(_Key)]() -> TCFuture<int>
			{
				co_await fg_Suspend();
				co_return pValue->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pValue' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
			}
		;
		co_return 0;
	}

	TCFuture<int> f_CopiedLookup(int _Key)
	{
		CValue *pValue = mp_Items.f_FindEqual(_Key);
		CValue *pCopy = pValue;
		auto fLambda = [this, pCaptured = pValue]() -> TCFuture<int>
			{
				co_await fg_Suspend();
				co_return pCaptured->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pCaptured' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
			}
		;
		co_await fg_Suspend();
		co_return pCopy->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'pCopy' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
	}

	TCFuture<int> f_CopiedAssignedLookup(int _Key)
	{
		CValue *pValue = nullptr;
		pValue = mp_Items.f_FindEqual(_Key);
		CValue *pCopy = pValue;
		co_await fg_Suspend();
		co_return pCopy->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'pCopy' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
	}

	TCFuture<int> f_RevalidationEnded(int _Key)
	{
		CValue *pValue = nullptr;
		{
			auto OnResume = co_await fg_OnResume
				(
					[&] -> CExceptionPointer
					{
						pValue = mp_Items.f_FindEqual(_Key);
						return {};
					}
				)
			;
		}
		co_await fg_Suspend();
		co_return pValue->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'pValue' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
	}

	TCFuture<int> f_AsyncDestroy(int _Key)
	{
		CValue *pValue = mp_Items.f_FindEqual(_Key);
		auto Cleanup = co_await fg_AsyncDestroy([]() -> TCFuture<int> { co_return 0; });
		co_return pValue->m_Value;
	}

	TCFuture<int> f_CaptureExceptions(int _Key)
	{
		CValue *pValue = mp_Items.f_FindEqual(_Key);
		co_await ECoroutineFlag_CaptureExceptions;
		auto CaptureScope = co_await g_CaptureExceptions;
		co_return pValue->m_Value;
	}

	CMap mp_Items;
	CInternal *mp_pInternal = nullptr;
	int m_Key = 0;
};
template TCFuture<int> CMyActor::f_Dependent<TCFuture<int>>(TCFuture<int> _Future);

struct CNotActor
{
	TCUnsafeFuture<int> f_Stale(int _Key)
	{
		CValue *pValue = m_Items.f_FindEqual(_Key);
		co_await fg_Suspend();
		co_return pValue->m_Value;
	}

	CMap m_Items;
};

template <typename t_CMap>
struct TCTemplateActor : CActor
{
	TCFuture<int> f_Dependent(int _Key)
	{
		auto *pValue = mp_Items.f_FindEqual(_Key);
		co_await fg_Suspend();
		co_return pValue->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'pValue' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
	}

	t_CMap mp_Items;
};
template struct TCTemplateActor<CMap>;

template <typename t_CMap>
struct TCTemplateRevalidation : CActor
{
	TCFuture<int> f_DependentHolder(int _Key)
	{
		auto *pValue = mp_Items.f_FindEqual(_Key);
		{
			auto OnResume = co_await fg_OnResume
				(
					[&] -> CExceptionPointer
					{
						pValue = mp_Items.f_FindEqual(_Key);
						return {};
					}
				)
			;
		}
		co_await fg_Suspend();
		co_return pValue->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'pValue' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
	}

	t_CMap mp_Items;
};
template struct TCTemplateRevalidation<CMap>;

// A free coroutine looks up the data of the actor of a parameter like `this`.
struct CParameterActor : CActor
{
	CMap m_Items;
};

TCFuture<int> fg_ParameterLookup(CParameterActor *_pActor, int _Key)
{
	CValue *pValue = _pActor->m_Items(_Key);
	co_await fg_Suspend();
	co_return pValue->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:12: warning: 'pValue' refers into the data of the actor and is used after a suspension, {{.*}}
}

// A lookup through a call operator of a member of a dependent type, and one
// chained after it.
template <typename t_CKey>
struct TCKeyMap
{
	CValue *operator () (t_CKey _Key);
};

struct CMaps
{
	CMap *operator () (int _Key);
};

template <typename t_CKey>
struct TCKeyedActor : CActor
{
	TCFuture<int> f_Get(t_CKey _Key)
	{
		CValue *pValue = m_Items(_Key);
		CValue *pInner = m_Maps(0)->f_FindEqual(1);
		co_await fg_Suspend();
		co_return pValue->m_Value + pInner->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'pValue' refers into the data of the actor and is used after a suspension, {{.*}}
// CHECK-MESSAGES: :[[@LINE-2]]:31: warning: 'pInner' refers into the data of the actor and is used after a suspension, {{.*}}
	}

	TCKeyMap<t_CKey> m_Items;
	CMaps m_Maps;
};
template struct TCKeyedActor<int>;

// A reference made from what fg_OnResume looks up again and compares, an
// await of a local lambda coroutine that never suspends, and a destroyed flag
// checked after the suspension.
struct CFlagged
{
	TCSharedPointer<bool> m_pDeleted;
	int m_Value = 0;
};

struct CFlaggedList
{
	CFlagged &f_Insert();
};

struct CRevalidatingActor : CActor
{
	TCFuture<int> f_Derived(int _Key)
	{
		CValue *pValue = nullptr;
		CValue *pInitial = nullptr;
		auto OnResume = co_await fg_OnResume
			(
				[&]() -> CExceptionPointer
				{
					pValue = mp_Items.f_FindEqual(_Key);
					if (!pValue || (pInitial && pValue != pInitial))
						return fg_Error();
					pInitial = pValue;
					return {};
				}
			)
		;
		auto &Value = *pValue;
		co_await fg_Suspend();
		co_return Value.m_Value;
	}

	TCFuture<int> f_NotCompared(int _Key)
	{
		CValue *pValue = nullptr;
		auto OnResume = co_await fg_OnResume
			(
				[&]() -> CExceptionPointer
				{
					pValue = mp_Items.f_FindEqual(_Key);
					return pValue ? CExceptionPointer() : fg_Error();
				}
			)
		;
		auto &Value = *pValue;
		co_await fg_Suspend();
		co_return Value.m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'Value' refers into the data of the actor and is used after a suspension, {{.*}}
	}

	TCFuture<int> f_NeverSuspends(int _Key)
	{
		CValue *pValue = mp_Items.f_FindEqual(_Key);
		auto fCheck = [](int _Value) -> TCFuture<int>
			{
				co_return _Value;
			}
		;
		co_await fCheck(1);
		co_return pValue->m_Value;
	}

	TCFuture<int> f_Flagged()
	{
		auto &Entry = mp_Flagged.f_Insert();
		auto pDeleted = Entry.m_pDeleted;
		auto *pEntry = &Entry;
		co_await fg_Suspend();
		if (*pDeleted)
			co_return 0;
		co_return pEntry->m_Value;
	}

	CMap mp_Items;
	CFlaggedList mp_Flagged;
};

// A handle that refers to the entry it found, like the `TCMapResult` that
// `TCMap::operator ()` returns.
template <typename t_CReturn>
struct TCMapResult
{
	bool f_WasCreated() const;
	t_CReturn operator * ();
	t_CReturn f_GetResult();
	operator t_CReturn ();

	t_CReturn mp_Return;
	bool mp_bWasCreated;
};

struct CHandleMap
{
	TCMapResult<CValue &> operator () (int _Key);
	CValue *begin();
	CValue *end();
};

struct CIterator
{
	CValue &operator * () const;
	CIterator &operator ++ ();
	bool operator != (CIterator const &_Other) const;
};

struct CIteratedMap
{
	CIterator begin();
	CIterator end();
};

struct CHandleActor : CActor
{
	TCFuture<int> f_Handle(int _Key)
	{
		auto Mapping = mp_Handles(_Key);
		if (!Mapping.f_WasCreated())
			co_return 0;
		auto &Entry = *Mapping;
		CValue Copy = *Mapping;
		co_await fg_Suspend();
		if (Mapping.f_WasCreated())
			Copy.m_Value = 1;
		Entry.m_Value = Copy.m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: 'Entry' refers into the data of the actor and is used after a suspension, {{.*}}
		co_return (*Mapping).m_Value + Mapping.f_GetResult().m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'Mapping' refers into the data of the actor and is used after a suspension, {{.*}}
	}

	TCFuture<int> f_HandleCopy(int _Key)
	{
		auto Mapping = mp_Handles(_Key);
		auto Other = Mapping;
		CValue &Converted = Other;
		co_await fg_Suspend();
		co_return Converted.m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'Converted' refers into the data of the actor and is used after a suspension, {{.*}}
	}

	TCFuture<int> f_LoopPointers()
	{
		int Sum = 0;
		for (auto &Entry : mp_Handles)
// CHECK-MESSAGES: :[[@LINE-1]]:22: warning: the loop goes on over the data of the actor after a suspension, {{.*}}
		{
			Sum += Entry.m_Value;
			co_await fg_Suspend();
		}
		co_return Sum;
	}

	TCFuture<int> f_LoopIterators()
	{
		int Sum = 0;
		CIteratedMap *pIterated = &mp_Iterated;
		for (auto &Entry : *pIterated)
// CHECK-MESSAGES: :[[@LINE-1]]:22: warning: the loop goes on over the data of the actor after a suspension, {{.*}}
		{
			co_await fg_Suspend();
			Sum += Entry.m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:11: warning: 'Entry' refers into the data of the actor and is used after a suspension, {{.*}}
		}
		co_return Sum;
	}

	// Leaving the loop at the suspension, or suspending before it, is fine.
	// Copying the entry doesn't help the iterator.
	TCFuture<int> f_LoopLeaves()
	{
		co_await fg_Suspend();
		for (auto &Entry : mp_Iterated)
		{
			if (Entry.m_Value)
			{
				co_await fg_Suspend();
				co_return 1;
			}
		}
		for (auto Entry : mp_Iterated)
// CHECK-MESSAGES: :[[@LINE-1]]:21: warning: the loop goes on over the data of the actor after a suspension, {{.*}}
			co_await fg_Suspend();
		co_return 0;
	}

	CHandleMap mp_Handles;
	CIteratedMap mp_Iterated;
};

// A flag copied from the entry that an fg_OnResume callback tests, and errors
// for, re-validates the entry.
struct CFlaggedEntry
{
	int m_Value = 0;
	TCSharedPointer<bool> m_pDestroyed;
};

struct CFlaggedMap
{
	CFlaggedEntry *f_FindEqual(int _Key);
};

struct CError
{
	CError();
	CError(char const *_pMessage);
	operator CExceptionPointer() const;
};

struct CFlagResumeActor : CActor
{
	TCFuture<int> f_Flagged(int _Key)
	{
		CFlaggedEntry *pEntry = mp_Entries.f_FindEqual(_Key);
		if (!pEntry)
			co_return 0;
		auto pDestroyed = pEntry->m_pDestroyed;
		auto OnResume = co_await fg_OnResume
			(
				[pDestroyed]() -> CExceptionPointer
				{
					if (*pDestroyed)
						return CError("Destroyed");
					return {};
				}
			)
		;
		co_await fg_Suspend();
		co_return pEntry->m_Value;
	}

	CFlaggedMap mp_Entries;
};

// A negated flag test leaves when the entry is there, and a member function of
// a handle other than f_WasCreated may read the entry.
struct CNegatedFlagActor : CActor
{
	TCFuture<int> f_Negated(int _Key)
	{
		CFlaggedEntry *pEntry = mp_Entries.f_FindEqual(_Key);
		auto pDestroyed = pEntry->m_pDestroyed;
		co_await fg_Suspend();
		if (!*pDestroyed)
			co_return 0;
		co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'pEntry' refers into the data of the actor and is used after a suspension, {{.*}}
	}

	TCFuture<int> f_HandleRead(int _Key)
	{
		auto Mapping = mp_Handles(_Key);
		co_await fg_Suspend();
		co_return Mapping.f_Value();
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'Mapping' refers into the data of the actor and is used after a suspension, {{.*}}
	}

	struct CValueHandle
	{
		bool f_WasCreated() const;
		int f_Value() const;
		CValue &m_Entry;
	};

	struct CValueHandles
	{
		CValueHandle operator () (int _Key);
	};

	CFlaggedMap mp_Entries;
	CValueHandles mp_Handles;
};

// A callback that can return success before it tests the copied flag, and a
// flag variable assigned another flag, don't re-validate the entry.
struct CFlagOrderActor : CActor
{
	TCFuture<int> f_EarlySuccess(int _Key, bool _bSkip)
	{
		CFlaggedEntry *pEntry = mp_Entries.f_FindEqual(_Key);
		auto pDestroyed = pEntry->m_pDestroyed;
		auto OnResume = co_await fg_OnResume
			(
				[pDestroyed, _bSkip]() -> CExceptionPointer
				{
					if (_bSkip)
						return {};
					if (*pDestroyed)
						return CError("Destroyed");
					return {};
				}
			)
		;
		co_await fg_Suspend();
		co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'pEntry' refers into the data of the actor and is used after a suspension, {{.*}}
	}

	TCFuture<int> f_Reassigned(int _Key, TCSharedPointer<bool> _pOther)
	{
		CFlaggedEntry *pEntry = mp_Entries.f_FindEqual(_Key);
		auto pDestroyed = pEntry->m_pDestroyed;
		pDestroyed = _pOther;
		co_await fg_Suspend();
		if (*pDestroyed)
			co_return 0;
		co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'pEntry' refers into the data of the actor and is used after a suspension, {{.*}}
	}

	CFlaggedMap mp_Entries;
};

// A pointer rebound after its flag was copied, a pointer from a handle,
// comparisons, an inherited handle, and a handle of a template.
struct CRound23Handle
{
	CValue &m_Entry;
	CValue *f_GetResult();
};

struct CRound23HandleBase
{
	CValue &m_Entry;
};

struct CRound23Derived : CRound23HandleBase
{
};

struct CRound23Maps
{
	CRound23Handle operator () (int _Key);
};

struct CRound23DerivedMaps
{
	CRound23Derived operator () (int _Key);
};

struct CRound23Actor : CActor
{
	TCFuture<int> f_Rebound()
	{
		CFlaggedEntry *pEntry = mp_Entries.f_FindEqual(1);
		auto pDestroyed = pEntry->m_pDestroyed;
		pEntry = mp_Entries.f_FindEqual(2);
		co_await fg_Suspend();
		if (*pDestroyed)
			co_return 0;
		co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'pEntry' refers into the data of the actor and is used after a suspension, {{.*}}
	}

	TCFuture<int> f_HandlePointer()
	{
		auto Mapping = mp_Handles(0);
		CValue *pValue = Mapping.f_GetResult();
		co_await fg_Suspend();
		co_return pValue->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'pValue' refers into the data of the actor and is used after a suspension, {{.*}}
	}

	TCFuture<bool> f_Compare()
	{
		CFlaggedEntry *pLeft = mp_Entries.f_FindEqual(0);
		CFlaggedEntry *pRight = mp_Entries.f_FindEqual(1);
		co_await fg_Suspend();
		co_return pLeft < pRight;
	}

	TCFuture<int> f_Inherited()
	{
		auto Mapping = mp_Derived(1);
		co_await fg_Suspend();
		co_return Mapping.m_Entry.m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'Mapping' refers into the data of the actor and is used after a suspension, {{.*}}
	}

	CFlaggedMap mp_Entries;
	CRound23Maps mp_Handles;
	CRound23DerivedMaps mp_Derived;
};

template <typename t_CHandle>
struct TCRound23Maps
{
	t_CHandle operator () (int _Key);
};

template <typename t_CHandle>
struct TCRound23Actor : CActor
{
	TCFuture<int> f_Template()
	{
		auto Mapping = mp_Handles(1);
		co_await fg_Suspend();
		co_return Mapping.m_Entry.m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'Mapping' refers into the data of the actor and is used after a suspension, {{.*}}
	}

	TCRound23Maps<t_CHandle> mp_Handles;
};

void fg_InstantiateRound23(TCRound23Actor<CRound23HandleBase> &_Actor)
{
	_Actor.f_Template();
}

// Round 24: a loop over a copy the collection returns, and a generic lambda.
struct CCopyable
{
	CValue *begin();
	CValue *end();
	CCopyable f_Copy() const;
};

struct CRound24Actor : CActor
{
	TCFuture<int> f_LoopCopy()
	{
		int Sum = 0;
		for (auto Value : mp_Values.f_Copy())
		{
			Sum += Value.m_Value;
			co_await fg_Suspend();
		}
		co_return Sum;
	}

	TCFuture<int> f_Generic()
	{
		auto fRead = [this](auto _Key) -> TCFuture<int>
			{
				auto Mapping = mp_Handles(_Key);
				co_await fg_Suspend();
				co_return Mapping.m_Entry.m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'Mapping' refers into the data of the actor and is used after a suspension, {{.*}}
			}
		;
		co_return co_await fRead(1);
	}

	CCopyable mp_Values;
	CRound23Maps mp_Handles;
};

// Round 25: an aggregate handle, and the actor of a parameter of a helper of
// another class or of a lambda.
struct CRound25Actor : CActor
{
	TCFuture<int> f_Aggregate()
	{
		CRound23HandleBase Handle{*mp_Entries.f_FindEqual(1)};
		co_await fg_Suspend();
		co_return Handle.m_Entry.m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'Handle' refers into the data of the actor and is used after a suspension, {{.*}}
	}

	struct CEntries
	{
		CValue *f_FindEqual(int _Key);
	};
	CEntries mp_Entries;
};

struct CRound25Helper
{
	TCFuture<int> f_Run(CRound25Actor *_pActor)
	{
		CValue *pValue = _pActor->mp_Entries.f_FindEqual(1);
		co_await fg_Suspend();
		co_return pValue->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'pValue' refers into the data of the actor and is used after a suspension, {{.*}}
	}
};

void fg_Round25Lambda()
{
	auto fRun = [](CRound25Actor *_pActor) -> TCFuture<int>
		{
			CValue *pValue = _pActor->mp_Entries.f_FindEqual(1);
			co_await fg_Suspend();
			co_return pValue->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: 'pValue' refers into the data of the actor and is used after a suspension, {{.*}}
		}
	;
	(void)fRun;
}

// Round 26: a copied member that isn't a removal flag, a lambda that captures
// the object of an actor, a generic lambda that gets one, and null tests.
struct CReadyEntry
{
	int m_Value = 0;
	TCSharedPointer<bool> m_pReady;
};

struct CReadyMap
{
	CReadyEntry *f_FindEqual(int _Key);
};

struct CRound26Actor : CActor
{
	TCFuture<int> f_NotRemovalFlag()
	{
		CReadyEntry *pEntry = mp_Entries.f_FindEqual(1);
		auto pReady = pEntry->m_pReady;
		co_await fg_Suspend();
		if (*pReady)
			co_return 0;
		co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'pEntry' refers into the data of the actor and is used after a suspension, {{.*}}
	}

	TCFuture<int> f_NullTest()
	{
		CReadyEntry *pEntry = mp_Entries.f_FindEqual(1);
		co_await fg_Suspend();
		if (pEntry)
			co_return 0;
		co_return 1;
	}

	CReadyMap mp_Entries;
};

TCFuture<int> fg_CapturesActor(CRound26Actor *_pActor)
{
	co_return co_await fg_CallSafe
		(
			[_pActor]() -> TCFuture<int>
			{
				CReadyEntry *pEntry = _pActor->mp_Entries.f_FindEqual(1);
				co_await fg_Suspend();
				co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, {{.*}}
			}
		)
	;
}

void fg_GenericActor(CRound26Actor *_pActor)
{
	[](auto *_pGeneric) -> TCFuture<int>
	{
		CReadyEntry *pEntry = _pGeneric->mp_Entries.f_FindEqual(1);
		co_await fg_Suspend();
		co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'pEntry' refers into the data of the actor and is used after a suspension, {{.*}}
	}(_pActor);
}

// Round 27: a lambda whose init capture looks up the data of an actor, a
// reference into a copy the loop owns, a removal flag through a handle, and a
// null test in a callback.
struct CRound27Entry
{
	int m_Value = 0;
	TCSharedPointer<bool> m_pDestroyed;
};

struct CRound27Handle
{
	CRound27Entry &m_Entry;
	CRound27Entry &operator * () const;
};

struct CRound27Map
{
	CRound27Entry *f_FindEqual(int _Key);
	CRound27Handle operator () (int _Key);
	CRound27Entry *begin();
	CRound27Entry *end();
	CRound27Map f_Copy() const;
};

struct CRound27Actor : CActor
{
	TCFuture<int> f_OwnedCopy()
	{
		int Sum = 0;
		for (auto &Entry : mp_Entries.f_Copy())
		{
			co_await fg_Suspend();
			Sum += Entry.m_Value;
		}
		co_return Sum;
	}

	TCFuture<int> f_HandleFlag()
	{
		auto Mapping = mp_Entries(1);
		auto pDestroyed = (*Mapping).m_pDestroyed;
		co_await fg_Suspend();
		if (*pDestroyed)
			co_return 0;
		co_return (*Mapping).m_Value;
	}

	TCFuture<int> f_NullComparison()
	{
		CRound27Entry *pEntry = mp_Entries.f_FindEqual(1);
		auto &Entry = *pEntry;
		auto OnResume = co_await fg_OnResume
			(
				[&]() -> CExceptionPointer
				{
					pEntry = mp_Entries.f_FindEqual(1);
					if (pEntry == nullptr)
						return CError("Gone");
					return {};
				}
			)
		;
		co_await fg_Suspend();
		co_return Entry.m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'Entry' refers into the data of the actor and is used after a suspension, {{.*}}
	}

	CRound27Map mp_Entries;
};

TCFuture<int> fg_InitCapture(CRound27Actor *_pActor)
{
	co_return co_await fg_CallSafe
		(
			[pEntry = _pActor->mp_Entries.f_FindEqual(1)]() -> TCFuture<int>
			{
				co_await fg_Suspend();
				co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, {{.*}}
			}
		)
	;
}

// A comparison re-validates what is made from a pointer only when the callback
// leaves with an error when it doesn't match, before it can return success.
struct CRound37Actor : CActor
{
	TCFuture<int> f_Skipped(bool _bSkip)
	{
		CValue *pValue = mp_Items.f_FindEqual(0);
		auto &Value = *pValue;
		auto OnResume = co_await fg_OnResume
			(
				[&]() -> CExceptionPointer
				{
					if (_bSkip)
						return {};
					if (pValue != mp_Items.f_FindEqual(0))
						return fg_Error();
					return {};
				}
			)
		;
		co_await fg_Suspend();
		co_return Value.m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'Value' refers into the data of the actor and is used after a suspension, {{.*}}
	}

	TCFuture<int> f_Discarded()
	{
		CValue *pValue = mp_Items.f_FindEqual(0);
		auto &Value = *pValue;
		auto OnResume = co_await fg_OnResume
			(
				[&]() -> CExceptionPointer
				{
					bool bSame = pValue == mp_Items.f_FindEqual(0);
					(void)bSame;
					return {};
				}
			)
		;
		co_await fg_Suspend();
		co_return Value.m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'Value' refers into the data of the actor and is used after a suspension, {{.*}}
	}

	TCFuture<int> f_Gated()
	{
		CValue *pValue = mp_Items.f_FindEqual(0);
		auto &Value = *pValue;
		auto OnResume = co_await fg_OnResume
			(
				[&]() -> CExceptionPointer
				{
					if (pValue != mp_Items.f_FindEqual(0))
						return fg_Error();
					return {};
				}
			)
		;
		co_await fg_Suspend();
		co_return Value.m_Value;
	}

	CMap mp_Items;
};

// Comparing two pointers kept from before doesn't tell the entry is there.
struct CStaleComparison : CActor
{
	TCFuture<int> f_Run(int _Key)
	{
		CValue *pValue = mp_Items.f_FindEqual(_Key);
		CValue *pSaved = pValue;
		auto OnResume = co_await fg_OnResume
			(
				[&, pSaved]() -> CExceptionPointer
				{
					if (pValue != pSaved)
						return fg_Error();
					return {};
				}
			)
		;
		co_await fg_Suspend();
		co_return pValue->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'pValue' refers into the data of the actor and is used after a suspension, {{.*}}
	}

	CMap mp_Items;
};

// A lambda that calls another one runs what that one captured.
struct CRound55Nested : CActor
{
	TCFuture<int> f_Run(int _Key)
	{
		CValue *pValue = mp_Items.f_FindEqual(_Key);
		auto fRead = [pValue] { return pValue->m_Value; };
		auto fOuter = [&fRead] { return fRead(); };
		co_await fg_Suspend();
		co_return fOuter();
// CHECK-MESSAGES: :[[@LINE-4]]:17: warning: 'pValue' refers into the data of the actor and is used after a suspension, {{.*}}
	}

	CMap mp_Items;
};

// A comparison saved in a local that nothing acts on doesn't re-validate.
struct CRound56Unused : CActor
{
	TCFuture<int> f_Run(int _Key)
	{
		CValue *pValue = mp_Items.f_FindEqual(_Key);
		auto OnResume = co_await fg_OnResume
			(
				[&]() -> CExceptionPointer
				{
					bool bSame = pValue == mp_Items.f_FindEqual(_Key);
					(void)bSame;
					return {};
				}
			)
		;
		co_await fg_Suspend();
		co_return pValue->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'pValue' refers into the data of the actor and is used after a suspension, {{.*}}
	}

	TCFuture<int> f_Acted(int _Key)
	{
		CValue *pValue = mp_Items.f_FindEqual(_Key);
		auto OnResume = co_await fg_OnResume
			(
				[&]() -> CExceptionPointer
				{
					bool bSame = pValue == mp_Items.f_FindEqual(_Key);
					if (!bSame)
						return fg_Error();
					return {};
				}
			)
		;
		co_await fg_Suspend();
		co_return pValue->m_Value;
	}

	CMap mp_Items;
};

// Either callback of a conditional registration re-validates only what both
// do, and what they assign is tracked after the registration ends.
struct CRound58Conditional : CActor
{
	TCFuture<int> f_Ended(bool _bChoose)
	{
		CValue *pValue = nullptr;
		{
			auto OnResume = _bChoose
				? co_await fg_OnResume
					(
						[&]() -> CExceptionPointer
						{
							pValue = mp_Items.f_FindEqual(1);
							return {};
						}
					)
				: co_await fg_OnResume
					(
						[&]() -> CExceptionPointer
						{
							pValue = mp_Items.f_FindEqual(2);
							return {};
						}
					)
			;
		}
		co_await fg_Suspend();
		co_return pValue ? pValue->m_Value : 0;
// CHECK-MESSAGES: :[[@LINE-1]]:22: warning: 'pValue' refers into the data of the actor and is used after a suspension, {{.*}}
	}

	TCFuture<int> f_Live(bool _bChoose)
	{
		CValue *pValue = mp_Items.f_FindEqual(1);
		auto OnResume = _bChoose
			? co_await fg_OnResume
				(
					[&]() -> CExceptionPointer
					{
						pValue = mp_Items.f_FindEqual(1);
						return {};
					}
				)
			: co_await fg_OnResume
				(
					[&]() -> CExceptionPointer
					{
						pValue = mp_Items.f_FindEqual(1);
						return {};
					}
				)
		;
		co_await fg_Suspend();
		co_return pValue ? pValue->m_Value : 0;
	}

	CMap mp_Items;
};

// A data member through a pointer to it is in the object; a value an
// operator gives is its own.
struct CRound73Values
{
	int operator[](int _Index) const
	{
		return _Index;
	}
	int operator()(int _Index) const
	{
		return _Index;
	}
};
struct CRound73Actor : CActor
{
	CMap mp_Items;
	CRound73Values mp_Values;

	TCFuture<int> f_MemberPointer()
	{
		auto pmItems = &CRound73Actor::mp_Items;
		CValue &Value = (this->*pmItems)[1];
		co_await fg_Suspend();
		co_return ++Value.m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'Value' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
	}

	TCFuture<int> f_ByValue()
	{
		int const &Value = mp_Values[1];
		int const &Called = mp_Values(2);
		co_await fg_Suspend();
		co_return Value + Called;
	}
};

// What `f_Get` gives is of an owning pointer only when the object is a
// pointer, and an element of a `std::array` held in the object lives with it.
struct CRound74Items
{
	int *m_pValues;
	int *f_Get();
	void f_Remove();
};
namespace std
{
	template <typename t_CType, decltype(sizeof(0)) t_nElements>
	struct array
	{
		t_CType m_Elements[t_nElements];
		t_CType &operator[](decltype(sizeof(0)) _Index);
	};
}
struct CRound74Actor : CActor
{
	CRound74Items m_Items;
	std::array<int, 2> m_Values;

	TCFuture<int> f_Get()
	{
		int *pValue = m_Items.f_Get();
		co_await fg_Suspend();
		co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: 'pValue' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
	}

	TCFuture<int> f_Array()
	{
		int &Value = m_Values[0];
		co_await fg_Suspend();
		co_return Value;
	}
};

// A cast to a reference, and a member through a pointer to it, are of the
// entry.
struct CRound75Entry
{
	int m_Value;
};
struct CRound75Items
{
	int &operator[](int _Key);
	CRound75Entry &f_Entry(int _Key);
};
struct CRound75Actor : CActor
{
	CRound75Items mp_Items;

	TCFuture<int> f_Cast()
	{
		int &Value = static_cast<int &>(mp_Items[1]);
		co_await fg_Suspend();
		co_return Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'Value' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
	}

	TCFuture<int> f_MemberPointer()
	{
		int CRound75Entry::*pmValue = &CRound75Entry::m_Value;
		int &Value = mp_Items.f_Entry(1).*pmValue;
		co_await fg_Suspend();
		co_return Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'Value' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
	}
};

// A free function that looks up in what it is given by reference.
template <typename t_CContainer, typename t_CKey>
auto fg_Round76FindEqual(t_CContainer &&_Container, t_CKey const &_Key) -> decltype(_Container.f_FindEqual(_Key))
{
	return _Container.f_FindEqual(_Key);
}
struct CRound76Actor : CActor
{
	CMap mp_Items;

	TCFuture<int> f_Free()
	{
		CValue *pValue = fg_Round76FindEqual(mp_Items, 1);
		co_await fg_Suspend();
		co_return pValue->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'pValue' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
	}
};

// The entries of a member array can't be removed.
struct CRound77Array : CActor
{
	int m_Values[2];

	TCFuture<int> f_Run()
	{
		for (int &Value : m_Values)
		{
			co_await fg_Suspend();
			Value = 42;
		}
		co_return 0;
	}
};

// What a lambda that runs after a suspension doesn't evaluate isn't a use.
struct CRound78Unevaluated : CActor
{
	CMap mp_Items;

	TCFuture<int> f_Size()
	{
		auto *pValue = mp_Items.f_FindEqual(1);
		auto fRead = [pValue]
			{
				return sizeof(*pValue);
			}
		;
		co_await fg_Suspend();
		co_return fRead();
	}

	TCFuture<int> f_Discarded()
	{
		auto *pValue = mp_Items.f_FindEqual(1);
		auto fRead = [pValue]
			{
				if constexpr (false)
					return pValue->m_Value;
				return 0;
			}
		;
		co_await fg_Suspend();
		co_return fRead();
	}
};

// Assigned a lookup through a cast to a reference.
struct CRound80Cast : CActor
{
	CMap mp_Items;

	TCFuture<int> f_Run()
	{
		CValue *pValue = nullptr;
		static_cast<CValue *&>(pValue) = mp_Items.f_FindEqual(1);
		co_await fg_Suspend();
		co_return pValue->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'pValue' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
	}
};

// A pointer that a helper gives back as it is.
template <typename t_CType>
t_CType const &fg_Round81Const(t_CType const &_Value) noexcept
{
	return _Value;
}
struct CRound81Const : CActor
{
	CMap mp_Items;

	TCFuture<int> f_Run()
	{
		CValue *pValue = mp_Items.f_FindEqual(1);
		CValue *pCopy = fg_Round81Const(pValue);
		co_await fg_Suspend();
		co_return pCopy->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'pCopy' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
	}
};

// A scope exit that is a discarded temporary runs its lambda where it is
// made.
struct CRound82Temporary : CActor
{
	CMap mp_Items;

	TCFuture<int> f_Dangle()
	{
		CValue *pEntry = mp_Items.f_FindEqual(1);
		co_await fg_Suspend();
		(void)(g_OnScopeExit / [pEntry]
// CHECK-MESSAGES: :[[@LINE-1]]:27: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
			{
				pEntry->m_Value = 1;
			})
		;
		co_return 0;
	}
};

// A lookup assigned to either of a conditional is in both.
struct CRound84Either : CActor
{
	CMap mp_Items;

	TCFuture<int> f_Run(bool _bFirst)
	{
		CValue *pFirst = nullptr;
		CValue *pSecond = nullptr;
		(_bFirst ? pFirst : pSecond) = &mp_Items[1];
		co_await fg_Suspend();
		co_return _bFirst ? pFirst->m_Value : pSecond->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:23: warning: 'pFirst' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
// CHECK-MESSAGES: :[[@LINE-2]]:41: warning: 'pSecond' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
	}
};

// What a lookup points to accessed after a suspension in the same
// expression.
struct CRound85Subscript : CActor
{
	CMap mp_Items;

	TCFuture<int> f_Run()
	{
		CValue *pEntry = mp_Items.f_FindEqual(0);
		co_return pEntry[(co_await fg_Suspend(), 0)].m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
	}
};

// Lambdas that call each other are followed however many there are; a swap
// through a cast to a reference swaps the variable; a static member named
// through an entry reads nothing of it.
struct CRound86Value
{
	int m_Value;
	static int fs_Count();
	static int ms_Count;
};
struct CRound86Map
{
	int *f_FindEqual(int _Key);
	CRound86Value *f_FindValue(int _Key);
	CRound86Value &operator[](int _Key);
};
template <typename t_CType>
void fg_Swap(t_CType &o_Left, t_CType &o_Right);
#define DRound86Link(d_This, d_Previous) auto f##d_This = [&f##d_Previous] { return f##d_Previous(); };
struct CRound86Actor : CActor
{
	CRound86Map m_Items;

	TCFuture<int> f_Chain()
	{
		int *pValue = m_Items.f_FindEqual(0);
		auto f0 = [pValue] { return *pValue; };
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: 'pValue' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
		DRound86Link(1, 0) DRound86Link(2, 1) DRound86Link(3, 2) DRound86Link(4, 3)
		DRound86Link(5, 4) DRound86Link(6, 5) DRound86Link(7, 6) DRound86Link(8, 7)
		DRound86Link(9, 8) DRound86Link(10, 9) DRound86Link(11, 10) DRound86Link(12, 11)
		DRound86Link(13, 12) DRound86Link(14, 13) DRound86Link(15, 14) DRound86Link(16, 15)
		DRound86Link(17, 16) DRound86Link(18, 17) DRound86Link(19, 18) DRound86Link(20, 19)
		co_await fg_Suspend();
		co_return f20();
	}

	TCFuture<int> f_Swap()
	{
		CRound86Value *pValue = nullptr;
		CRound86Value *pFound = m_Items.f_FindValue(1);
		fg_Swap(static_cast<CRound86Value *&>(pValue), pFound);
		co_await fg_Suspend();
		co_return pValue->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'pValue' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
	}

	TCFuture<int> f_Static()
	{
		CRound86Value &Value = m_Items[1];
		CRound86Value *pValue = m_Items.f_FindValue(1);
		co_await fg_Suspend();
		co_return Value.fs_Count() + Value.ms_Count + pValue->fs_Count();
	}
};

// Through a GNU `?:`, and through many steps of pointer arithmetic.
TCFuture<int> fg_Round90Work();
struct CRound90Value
{
	int m_Value;
};
struct CRound90Map
{
	CRound90Value *f_FindEqual(int _Key);
};
struct CRound90Actor : CActor
{
	CRound90Map mp_Items;

	TCFuture<int> f_Elvis()
	{
		CRound90Value *pFound = mp_Items.f_FindEqual(0);
		CRound90Value *pValue = pFound ?: nullptr;
		co_await fg_Round90Work();
		co_return pValue->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'pValue' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
	}

	TCFuture<int> f_Offset()
	{
		CRound90Value *pValue = mp_Items.f_FindEqual(0) + 0 + 0 + 0 + 0 + 0;
		co_await fg_Round90Work();
		co_return pValue->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'pValue' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
	}
};

// A lookup that a coroutine lambda that `fg_CallSafe` runs assigns.
struct CRound91Map
{
	int *f_FindEqual(int _Key);
};
struct CRound91Actor : CActor
{
	CRound91Map m_Items;
	TCFuture<int> f_Run()
	{
		int *pValue = nullptr;
		co_await fg_CallSafe
			(
				[&]() -> TCFuture<int>
				{
					pValue = m_Items.f_FindEqual(1);
					co_return 0;
				}
			)
		;
		co_await fg_Suspend();
		co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: 'pValue' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
	}
};

// A loop over a `std::array`, which keeps its elements.
namespace std
{
	template <typename t_CType, decltype(sizeof(0)) t_nElements>
	t_CType *begin(array<t_CType, t_nElements> &_Array);
	template <typename t_CType, decltype(sizeof(0)) t_nElements>
	t_CType *end(array<t_CType, t_nElements> &_Array);
}
struct CRound91Array : CActor
{
	std::array<int, 2> m_Values;
	TCFuture<int> f_Run()
	{
		for (int Value : m_Values)
			co_await fg_Suspend();
		co_return 0;
	}
};

// Through a local pointer to a local pointer to an entry.
struct CRound92Map
{
	int *f_FindEqual(int _Key);
};
struct CRound92Actor : CActor
{
	CRound92Map m_Items;
	TCFuture<int> f_Read()
	{
		int *pValue = m_Items.f_FindEqual(0);
		int **ppValue = &pValue;
		co_await fg_Suspend();
		co_return **ppValue;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pValue' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
	}
};

// Nested lambdas run in the order they are called: a read in one called
// before a refresh is stale, and a refresh called for sure after one called
// maybe still refreshes.
struct CRound97Actor : CActor
{
	CMap mp_Items;

	TCFuture<int> f_ReadBeforeRefresh()
	{
		CValue *pValue = mp_Items.f_FindEqual(1);
		auto fUse = [&] { return pValue->m_Value; };
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pValue' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
		auto fHelper = [&] { return fUse(); };
		auto fRefresh = [&] { pValue = mp_Items.f_FindEqual(1); };
		auto fBoth = [&] { fHelper(); fRefresh(); };
		co_await fg_Suspend();
		fBoth();
		co_return 0;
	}

	TCFuture<int> f_RefreshBeforeRead()
	{
		CValue *pValue = mp_Items.f_FindEqual(1);
		auto fUse = [&] { return pValue->m_Value; };
		auto fRefresh = [&] { pValue = mp_Items.f_FindEqual(1); };
		auto fHelper = [&] { fRefresh(); };
		auto fBoth = [&] { fHelper(); return fUse(); };
		co_await fg_Suspend();
		co_return fBoth();
	}

	TCFuture<int> f_MaybeThenSure(bool _bMaybe)
	{
		CValue *pValue = mp_Items.f_FindEqual(1);
		auto fRefresh = [&] { pValue = mp_Items.f_FindEqual(1); };
		auto fBoth = [&] { if (_bMaybe) fRefresh(); fRefresh(); };
		co_await fg_Suspend();
		fBoth();
		co_return pValue->m_Value;
	}
};

// A lambda called again by another runs again, with what was assigned in
// between.
struct CRound111Map
{
	int *f_FindEqual(int _Key);
};
TCFuture<int> fg_Round111Suspend();
struct CRound111Actor : CActor
{
	CRound111Map m_Items;
	TCFuture<int> f_Run()
	{
		int *pValue = m_Items.f_FindEqual(0);
		int Local = 0;
		auto fRefresh = [&]
			{
				pValue = m_Items.f_FindEqual(0);
			}
		;
		auto fRun = [&]
			{
				fRefresh();
				pValue = &Local;
				fRefresh();
			}
		;
		fRun();
		co_await fg_Round111Suspend();
		co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: 'pValue' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
	}
};

// Past as many lambda calls as are followed, what a lambda reads is still
// looked at.
struct CRound112Value
{
	int m_Value;
};
struct CRound112Map
{
	CRound112Value *f_FindEqual(int);
};
#define DRound112Step(d_Index, d_Previous) auto f##d_Index = [&] { f##d_Previous(); f##d_Previous(); }
struct CRound112Budget : CActor
{
	CRound112Map mp_Items;

	TCFuture<int> f_Run()
	{
		auto *pValue = mp_Items.f_FindEqual(0);
		auto fRead = [pValue] { return pValue->m_Value; };
// CHECK-MESSAGES: :[[@LINE-1]]:17: warning: 'pValue' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
		auto f0 = [] {};
		DRound112Step(1, 0); DRound112Step(2, 1); DRound112Step(3, 2);
		DRound112Step(4, 3); DRound112Step(5, 4); DRound112Step(6, 5);
		DRound112Step(7, 6); DRound112Step(8, 7); DRound112Step(9, 8);
		auto fRun = [&] { f9(); return fRead(); };
		co_await fg_Suspend();
		co_return fRun();
	}
};

// A subscript whose index suspends uses the pointer it was given before.
struct CRound112IntMap
{
	int &operator[](int);
};
struct CRound112Subscript : CActor
{
	CRound112IntMap m_Items;

	TCFuture<int> f_Run()
	{
		co_return (&m_Items[0])[(co_await fg_Suspend(), 0)];
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: the data of the actor that this call uses is looked up before its arguments suspend, during which other calls of the actor can change or erase the data; look it up after the suspension [malterlib-coroutine-actor-reference]
	}
};

// Nested helpers that give back what they are given are decided once.
template <typename t_CType>
t_CType const &fg_Round112Const(t_CType const &_Value)
{
	return _Value;
}
#define DRound112Const4(d_Value) fg_Round112Const(fg_Round112Const(fg_Round112Const(fg_Round112Const(d_Value))))
int g_Round112Value;
struct CRound112Nested : CActor
{
	TCFuture<int> f_Run()
	{
		int *p = &g_Round112Value;
		int *q = DRound112Const4(DRound112Const4(DRound112Const4(DRound112Const4(DRound112Const4(DRound112Const4(DRound112Const4(p)))))));
		co_await fg_Suspend();
		co_return *q;
	}
};

// A static member of an entry isn't in the entry.
struct CRound112StaticEntry
{
	static constexpr int c_Value = 7;
};
struct CRound112StaticMap
{
	CRound112StaticEntry &operator[](int);
};
struct CRound112Static : CActor
{
	CRound112StaticMap m_Items;

	TCFuture<int> f_Run()
	{
		int const &Value = m_Items[0].c_Value;
		int const *pValue = &m_Items[0].c_Value;
		co_await fg_Suspend();
		co_return Value + *pValue;
	}
};

// A call that can't run isn't reported.
struct CRound112ReadEntry
{
	int f_Read(int);
};
struct CRound112ReadMap
{
	CRound112ReadEntry &operator[](int);
};
struct CRound112Unreachable : CActor
{
	CRound112ReadMap m_Items;

	TCFuture<int> f_Run()
	{
		if (false)
			co_return m_Items[0].f_Read(co_await fg_Suspend());
		co_return 0;
		m_Items[0].f_Read(co_await fg_Suspend());
	}
};

// Assigning through a captured reference uses it, also from a temporary scope exit at the end of its full expression.
namespace NRound118ReferenceWriteAndExit {
TCFuture<int> work();
struct CItems { int &operator[](int _Key); };
struct Test : CActor {
  CItems m_Items;
  TCFuture<int> dangling() {
    int &entry = m_Items[0];
    auto write = [&entry] { entry = 1; };
// CHECK-MESSAGES: :[[@LINE-1]]:20: warning: 'entry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
    co_await work();
    write();
    co_return 0;
  }
  TCFuture<int> f() {
    int &entry = m_Items[0];
    ((void)(g_OnScopeExit / [&] { ++entry; }), co_await work());
// CHECK-MESSAGES: :[[@LINE-1]]:30: warning: 'entry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
    co_return 0;
  }
};
}

// Round 119: a lookup in a statement expression or returned by a lambda
// called in place, and a pointer redirected before a comma or an identity
// function.
namespace NRound119Wrapped
{
struct CValue { int m_n; };
struct CMap { CValue *f_FindEqual(int); };
TCFuture<int> fg_Work();
CValue *fg_Identity(CValue *_pValue) { return _pValue; }

struct CRunner : CActor {
  CMap m_Items;

  TCFuture<int> f_Statement() {
    auto pValue = ({ m_Items.f_FindEqual(1); });
    co_await fg_Work();
    co_return pValue->m_n;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pValue' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }

  TCFuture<int> f_Lambda() {
    auto pValue = [this] { return m_Items.f_FindEqual(1); }();
    co_await fg_Work();
    co_return pValue->m_n;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pValue' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }

  TCFuture<int> f_Redirected() {
    CValue Local{42};
    auto pValue = m_Items.f_FindEqual(1);
    pValue = &Local;
    auto pCopy = (void(0), pValue);
    auto pOther = fg_Identity(pValue);
    co_await fg_Work();
    co_return pCopy->m_n + pOther->m_n;
  }
};
}

// Round 120: comparing a pointer with a cast or an offset of a saved copy of
// it doesn't look it up again.
namespace NRound120SavedCast
{
struct CValue { int m_X; };
struct CMap {
  CValue *f_FindEqual(int);
  void f_Clear();
};
struct CExceptionPointer {
  CExceptionPointer();
  CExceptionPointer(decltype(nullptr));
};
CExceptionPointer fg_Error();
struct COnResume { ~COnResume(); };
template <typename tf_FCallback>
TCFuture<COnResume> fg_OnResume(tf_FCallback &&_fCallback) { return {}; }
TCFuture<int> fg_Pause();

struct CRunner : CActor {
  CMap m_Items;

  TCFuture<int> f_Cast() {
    CValue *pValue = m_Items.f_FindEqual(1);
    void *pSaved = pValue;
    auto Registration = co_await fg_OnResume(
      [&]() -> CExceptionPointer {
        if (pValue != static_cast<CValue *>(pSaved))
          return fg_Error();
        return nullptr;
      });
    co_await fg_Pause();
    co_return pValue->m_X;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pValue' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
};
}

// Round 122: a lambda run by `std::invoke` uses what it captures where it is
// run.
namespace std {
template <typename t_FFunction>
auto invoke(t_FFunction &&_fFunction) -> decltype(_fFunction()) { return _fFunction(); }
}

namespace NRound122Invoke
{
struct CMap { int *f_FindEqual(int); };
TCFuture<int> fg_Pause();

struct CRunner : CActor {
  CMap m_Items;

  TCFuture<int> f_Dangling() {
    int *pEntry = m_Items.f_FindEqual(0);
    auto fRead = [pEntry] { return *pEntry; };
// CHECK-MESSAGES: :[[@LINE-1]]:19: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
    co_await fg_Pause();
    co_return std::invoke(fRead);
  }
};
}

// Round 124: an operator of an owning pointer called by its name is like the
// operator.
namespace NRound124
{
TCFuture<int> fg_Suspend();
struct CValue {
  int m_Value;
};

struct CApp : CActor {
  TCSharedPointer<int> mp_pOwner;
  TCSharedPointer<CValue> mp_pValue;

  TCFuture<int> f_Star() {
    int &Value = mp_pOwner.operator*();
    co_await fg_Suspend();
    co_return Value;
  }

  TCFuture<int> f_Arrow() {
    CValue *pValue = mp_pValue.operator->();
    co_await fg_Suspend();
    co_return pValue->m_Value;
  }
};
}

// Round 129: an element through a lookup assigned before, `pEntry[0]`, is
// into what it looked up, like `*pEntry`.
namespace NRound129
{
TCFuture<int> fg_Work();
struct CItem {
  int m_Value;
};
struct CMap {
  CItem *f_FindEqual(int _Key);
};

struct CApp : CActor {
  CMap mp_Items;

  TCFuture<int> f_Run() {
    CItem *pItem = nullptr;
    pItem = mp_Items.f_FindEqual(0);
    CItem &Entry = pItem[0];
    co_await fg_Work();
    co_return Entry.m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'Entry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
};
}

// Round 133: an assignment through a reference bound to either of a
// conditional may assign either, and one through any number of references
// assigns what they are bound to.
namespace NRound133
{
TCFuture<int> fg_Suspend();
struct CMap {
  int &operator[](int _Key);
};

struct CApp : CActor {
  CMap mp_Items;

  TCFuture<int> f_Either(bool _bChoose) {
    int *pFirst = nullptr, *pSecond = nullptr;
    int *&pAlias = _bChoose ? pFirst : pSecond;
    pAlias = &mp_Items[0];
    co_await fg_Suspend();
    co_return pFirst ? *pFirst : 0;
// CHECK-MESSAGES: :[[@LINE-1]]:25: warning: 'pFirst' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }

  TCFuture<int> f_Refreshed() {
    int *pValue = &mp_Items[0];
    int *&a = pValue;
    int *&b = a;
    int *&c = b;
    int *&d = c;
    int *&e = d;
    co_await fg_Suspend();
    e = &mp_Items[0];
    co_return *pValue;
  }
};
}

namespace NRound143 {
TCFuture<int> fg_Suspend();

// A lambda that another runs by `std::invoke`.
struct CItems {
  int *f_FindEqual(int _Key);
};

struct CApp : CActor {
  CItems mp_Items;
  TCFuture<int> f_Nested() {
    int *pValue = mp_Items.f_FindEqual(0);
    auto fRead = [pValue] { return *pValue; };
// CHECK-MESSAGES: :[[@LINE-1]]:19: warning: 'pValue' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
    auto fWrapper = [&fRead] { return std::invoke(fRead); };
    co_await fg_Suspend();
    co_return fWrapper();
  }
};
}

namespace std {
template <typename tf_CTo, typename tf_CFrom>
tf_CTo bit_cast(tf_CFrom const &_From) { return __builtin_bit_cast(tf_CTo, _From); }
}

namespace NRound144 {
TCFuture<int> fg_Work();

template <typename tf_CTo, typename tf_CFrom>
tf_CTo fg_BitCast(tf_CFrom const &_From) { return std::bit_cast<tf_CTo>(_From); }

struct CItems {
  int &operator[](int _Key);
  int *f_FindEqual(int _Key);
};

struct CApp : CActor {
  CItems mp_Items;
  int m_Value = 0;

  // Through the bits of a pointer, `fg_BitCast<int *>(&mp_Items[0])`.
  TCFuture<int> f_BitCast() {
    int *pValue = fg_BitCast<int *>(&mp_Items[0]);
    co_await fg_Work();
    co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pValue' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }

  // A member function of the actor gives what it returns.
  int *f_Identity(int *_pValue) { return _pValue; }
  int &f_Value() { return m_Value; }
  int *f_Find() { return mp_Items.f_FindEqual(0); }

  TCFuture<int> f_Local() {
    int Local = 42;
    int *pValue = f_Identity(&Local);
    co_await fg_Work();
    co_return *pValue;
  }

  TCFuture<int> f_Member() {
    int &Value = f_Value();
    co_await fg_Work();
    co_return Value;
  }

  TCFuture<int> f_Lookup() {
    int *pValue = f_Find();
    co_await fg_Work();
    co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pValue' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
};
}

namespace NRound154 {
TCFuture<int> fg_Work();
struct CItems {
  int *f_FindEqual(int _Key);
};
struct CApp : CActor {
  CItems mp_Items;
  // Written after the right side of an assignment, which runs first, also a
  // compound one.
  TCFuture<int> f_Compound() {
    int *pValue = mp_Items.f_FindEqual(1);
    *pValue += co_await fg_Work();
// CHECK-MESSAGES: :[[@LINE-1]]:6: warning: 'pValue' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
    co_return 0;
  }
  // Where a stepped pointer pointed, accessed after a suspension.
  TCFuture<int> f_Stepped() {
    int *pValue = mp_Items.f_FindEqual(1);
    co_return (pValue++)[(co_await fg_Work(), 0)];
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: the data of the actor that this call uses is looked up before its arguments suspend, during which other calls of the actor can change or erase the data; look it up after the suspension [malterlib-coroutine-actor-reference]
// CHECK-MESSAGES: :[[@LINE-2]]:16: warning: 'pValue' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
};
}

namespace NRound156 {
TCFuture<int> fg_Work();
int g_Global;
struct CItems {
  int *f_FindEqual(int _Key);
};
int *fg_Choose(int *_pFallback, int *_pEntry) {
  _pFallback = _pEntry;
  return _pFallback;
}
struct CApp : CActor {
  CItems mp_Items;
  // Through a helper that returns a parameter it assigned the entry.
  TCFuture<int> f_Chosen() {
    int *pValue = fg_Choose(&g_Global, mp_Items.f_FindEqual(0));
    co_await fg_Work();
    co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pValue' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
};
}

namespace NRound159 {
TCFuture<int> fg_Work();
struct CEntry {
  int m_Value;
  TCSharedPointer<bool> m_pDeleted;
  ~CEntry() { *m_pDeleted = true; }
};
struct CItems {
  CEntry *m_pEntry;
  CEntry *f_FindEqual(int) { return m_pEntry; }
  void f_Remove(int) {
    delete m_pEntry;
    m_pEntry = nullptr;
  }
};
// A helper that removes through a reference it is given.
void fg_Erase(CItems &_Items) { _Items.f_Remove(0); }
// A helper that gives the address of what it is given.
int *fg_Address(int &_Value) { return &_Value; }
struct CRanges {
  int &operator[](int _Index);
};
struct CApp : CActor {
  CItems m_Items;
  int m_Value;
  CRanges m_Ranges;
  TCFuture<int> f_Erased() {
    auto &Entry = *m_Items.f_FindEqual(0);
    auto pDeleted = Entry.m_pDeleted;
    co_await fg_Work();
    if (*pDeleted)
      co_return 0;
    fg_Erase(m_Items);
    co_return Entry.m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'Entry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_Member() {
    int *pValue = fg_Address(m_Value);
    co_await fg_Work();
    co_return *pValue;
  }
  // Laundered, it is the same.
  TCFuture<int> f_Laundered() {
    int *pValue = &m_Ranges[0];
    pValue = __builtin_launder(pValue);
    co_await fg_Work();
    co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pValue' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
};
}

namespace NRound160 {
TCFuture<int> fg_Suspend();
struct CItems {
  int *f_FindEqual(int);
};
struct CApp : CActor {
  CItems mp_Items;
  // Through a helper of the actor that gives back what it is given, cast or
  // through a local.
  int *f_Forward(int *_pValue) { return static_cast<int *>(_pValue); }
  int *f_Copy(int *_pValue) {
    int *pCopy = _pValue;
    return pCopy;
  }
  TCFuture<int> f_Forwarded() {
    int *pValue = f_Forward(mp_Items.f_FindEqual(0));
    co_await fg_Suspend();
    co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pValue' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_Copied() {
    int *pValue = f_Copy(mp_Items.f_FindEqual(0));
    co_await fg_Suspend();
    co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pValue' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
};
}

namespace NRound166 {
// A function whose control flow is too complex to build isn't accepted, like
// one through a chain of default arguments of conditionals with a temporary
// whose destructor doesn't return, which each of its exponentially many paths
// needs.
struct CTemporary {
  [[noreturn]] ~CTemporary();
};
extern bool g_bChoose;
int fg_Link0(CTemporary const & = CTemporary{});
#define DLink(d_Next, d_Previous)                                              \
  int fg_Link##d_Next(int = g_bChoose ? fg_Link##d_Previous()                  \
                                      : fg_Link##d_Previous());
DLink(1, 0) DLink(2, 1) DLink(3, 2) DLink(4, 3) DLink(5, 4) DLink(6, 5)
DLink(7, 6) DLink(8, 7) DLink(9, 8) DLink(10, 9) DLink(11, 10) DLink(12, 11)
DLink(13, 12) DLink(14, 13)
#undef DLink
struct CMap {
  int &operator[](int _Key);
};
TCFuture<int> fg_Work();
struct CApp : CActor {
  CMap m_Items;
  TCFuture<int> f_Complex() {
// CHECK-MESSAGES: :[[@LINE-1]]:17: warning: the control flow of 'f_Complex' is too complex to analyze, so whether it uses what refers into the data of the actor after a suspension can't be told; split up what makes it complex [malterlib-coroutine-actor-reference]
    int *pValue = &m_Items[0];
    co_await fg_Work();
    fg_Link14();
    co_return *pValue;
  }
};
}

namespace NRound168 {
TCFuture<int> fg_Work();
struct CMap {
  int &operator[](int _Key);
  void f_Clear();
};
// Pointed at the data by a function it is given to by reference.
void fg_Set(int *&o_pValue, int *_pValue) { o_pValue = _pValue; }
struct CApp : CActor {
  CMap m_Items;
  TCFuture<int> f_Set() {
    int *pValue = nullptr;
    fg_Set(pValue, &m_Items[0]);
    co_await fg_Work();
    co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pValue' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
};
// Not looked up again by a helper that gives back what was saved.
struct COnResume {
  ~COnResume();
};
struct CExceptionPointer {
  CExceptionPointer();
};
template <typename tf_FCallback>
TCFuture<COnResume> fg_OnResume(tf_FCallback &&_fCallback);
int *g_pSaved;
int *fg_LoadSaved() { return g_pSaved; }
void fg_Restore(int *&o_pValue) { o_pValue = fg_LoadSaved(); }
struct CRestoring : CActor {
  CMap m_Items;
  TCFuture<int> f_Run() {
    int *pValue = &m_Items[0];
    g_pSaved = pValue;
    auto Guard = co_await fg_OnResume([&]() -> CExceptionPointer {
      fg_Restore(pValue);
      return {};
    });
    co_await fg_Work();
    co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pValue' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
};
}

namespace NRound171 {
struct CEntry {
  int m_Value;
  TCSharedPointer<bool> m_pDeleted;
  ~CEntry() { *m_pDeleted = true; }
};
struct CItems {
  CEntry *m_pEntry;
  CEntry *f_FindEqual(int _Key) { return m_pEntry; }
  void f_Clear() {
    delete m_pEntry;
    m_pEntry = nullptr;
  }
};
TCFuture<int> fg_Suspend();
struct CApp : CActor {
  CItems *mp_pItems;
  CItems mp_Items;
  // A const member function whose body frees, through a pointer member.
  void f_Clear() const { mp_pItems->f_Clear(); }
  TCFuture<int> f_Const() {
    auto *pEntry = mp_pItems->f_FindEqual(0);
    auto pDeleted = pEntry->m_pDeleted;
    co_await fg_Suspend();
    if (*pDeleted)
      co_return 0;
    f_Clear();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  // A scope exit that frees where it ends.
  TCFuture<int> f_ScopeExit() {
    auto *pEntry = mp_Items.f_FindEqual(0);
    auto pDeleted = pEntry->m_pDeleted;
    co_await fg_Suspend();
    if (*pDeleted)
      co_return 0;
    {
      auto Exit = g_OnScopeExit / [this] { mp_Items.f_Clear(); };
    }
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  // Not where a negated test tells it isn't removed.
  TCFuture<int> f_Negated() {
    auto *pEntry = mp_Items.f_FindEqual(0);
    auto pDeleted = pEntry->m_pDeleted;
    co_await fg_Suspend();
    if (!*pDeleted)
      co_return pEntry->m_Value;
    co_return 0;
  }
};
}

namespace NRound174 {
struct CMap { int *f_FindEqual(int) const; };
TCFuture<int> fg_Work();
struct COnResume {
  ~COnResume();
};
struct CExceptionPointer {
  CExceptionPointer();
};
template <typename tf_FCallback>
TCFuture<COnResume> fg_OnResume(tf_FCallback &&_fCallback);
// A helper that looks it up again and then puts back what was saved.
void fg_Restore(int *&o_pValue, int *_pSaved) { o_pValue = _pSaved; }
void fg_RefreshThenRestore(CMap &_Map, int *&o_pValue, int *_pSaved) {
  o_pValue = _Map.f_FindEqual(0);
  fg_Restore(o_pValue, _pSaved);
}
// One that looks it up again through another.
void fg_Lookup(CMap const &_Map, int *&o_pValue) {
  o_pValue = _Map.f_FindEqual(0);
}
void fg_Refresh(CMap const &_Map, int *&o_pValue, int *_pSaved) {
  o_pValue = _pSaved;
  fg_Lookup(_Map, o_pValue);
}
// A virtual one whose override can look it up again.
struct CRefresher {
  virtual void f_Refresh(int *&o_pValue) {}
};
struct CApp : CActor {
  CMap m_Items;
  CRefresher *m_pRefresher;
  TCFuture<int> f_Restored() {
    int *pValue = m_Items.f_FindEqual(0);
    int *pSaved = pValue;
    auto Guard = co_await fg_OnResume([&]() -> CExceptionPointer {
      fg_RefreshThenRestore(m_Items, pValue, pSaved);
      return {};
    });
    co_await fg_Work();
    co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pValue' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_Refreshed() {
    int *pValue = m_Items.f_FindEqual(0);
    int *pSaved = pValue;
    auto Guard = co_await fg_OnResume([&]() -> CExceptionPointer {
      fg_Refresh(m_Items, pValue, pSaved);
      return {};
    });
    co_await fg_Work();
    co_return *pValue;
  }
  TCFuture<int> f_Virtual() {
    int *pValue = m_Items.f_FindEqual(0);
    auto Guard = co_await fg_OnResume([&]() -> CExceptionPointer {
      m_pRefresher->f_Refresh(pValue);
      return {};
    });
    co_await fg_Work();
    co_return *pValue;
  }
  // Looked up again in a lambda it calls where it is.
  TCFuture<int> f_Invoked() {
    int *pValue = m_Items.f_FindEqual(0);
    auto Guard = co_await fg_OnResume([&]() -> CExceptionPointer {
      [&] { pValue = m_Items.f_FindEqual(0); }();
      return {};
    });
    co_await fg_Work();
    co_return *pValue;
  }
  // Not in a copy of it.
  TCFuture<int> f_InvokedCopy() {
    int *pValue = m_Items.f_FindEqual(0);
    auto Guard = co_await fg_OnResume([&]() -> CExceptionPointer {
      [=, this]() mutable { pValue = m_Items.f_FindEqual(0); }();
      return {};
    });
    co_await fg_Work();
    co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pValue' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  // Also through a chain of helpers that each call the one before three
  // times, each looked into once at the least depth it is reached at: here
  // the first is reached past the depth looked into first, and then where
  // it is.
  TCFuture<int> f_InvokedChain() {
    int *pValue = m_Items.f_FindEqual(0);
    auto Guard = co_await fg_OnResume([&]() -> CExceptionPointer {
      auto fLookup = [&] { pValue = m_Items.f_FindEqual(0); };
      auto f0 = [&] { fLookup(); };
      auto f1 = [&] { f0(); f0(); f0(); };
      auto f2 = [&] { f1(); f1(); f1(); };
      auto f3 = [&] { f2(); f2(); f2(); };
      auto f4 = [&] { f3(); f3(); f3(); };
      auto f5 = [&] { f4(); f4(); f4(); };
      auto f6 = [&] { f5(); f5(); f5(); };
      auto f7 = [&] { f6(); f6(); f6(); };
      auto f8 = [&] { f7(); f7(); f7(); };
      auto f9 = [&] { f8(); f8(); f8(); };
      auto f10 = [&] { f9(); f9(); f9(); };
      auto f11 = [&] { f10(); f10(); f10(); };
      auto f12 = [&] { f11(); f11(); f11(); };
      auto f13 = [&] { f12(); f12(); f12(); };
      auto f14 = [&] { f13(); f13(); f13(); };
      auto f15 = [&] { f14(); f14(); f14(); };
      f15();
      f0();
      return {};
    });
    co_await fg_Work();
    co_return *pValue;
  }
  TCFuture<int> f_InvokedChainTooDeep() {
    int *pValue = m_Items.f_FindEqual(0);
    auto Guard = co_await fg_OnResume([&]() -> CExceptionPointer {
      auto fLookup = [&] { pValue = m_Items.f_FindEqual(0); };
      auto f0 = [&] { fLookup(); };
      auto f1 = [&] { f0(); f0(); f0(); };
      auto f2 = [&] { f1(); f1(); f1(); };
      auto f3 = [&] { f2(); f2(); f2(); };
      auto f4 = [&] { f3(); f3(); f3(); };
      auto f5 = [&] { f4(); f4(); f4(); };
      auto f6 = [&] { f5(); f5(); f5(); };
      auto f7 = [&] { f6(); f6(); f6(); };
      auto f8 = [&] { f7(); f7(); f7(); };
      auto f9 = [&] { f8(); f8(); f8(); };
      auto f10 = [&] { f9(); f9(); f9(); };
      auto f11 = [&] { f10(); f10(); f10(); };
      auto f12 = [&] { f11(); f11(); f11(); };
      auto f13 = [&] { f12(); f12(); f12(); };
      auto f14 = [&] { f13(); f13(); f13(); };
      auto f15 = [&] { f14(); f14(); f14(); };
      f15();
      return {};
    });
    co_await fg_Work();
    co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pValue' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  // Nor after what can leave it.
  TCFuture<int> f_InvokedLeaving(bool _bSkip) {
    int *pValue = m_Items.f_FindEqual(0);
    auto Guard = co_await fg_OnResume([&]() -> CExceptionPointer {
      [&] {
        if (_bSkip)
          return;
        pValue = m_Items.f_FindEqual(0);
      }();
      return {};
    });
    co_await fg_Work();
    co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pValue' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
};
}

namespace NRound175 {
TCFuture<int> fg_Work();
// A member of the actor that a getter gives, through a parameter that is the
// object of the actor.
struct CApp : CActor {
  int m_Number;
  int &f_Number() { return m_Number; }
};
TCFuture<int> fg_Read(CApp *_pThis) {
  int &Number = _pThis->f_Number();
  co_await fg_Work();
  co_return Number;
}
}

namespace NRound177 {
struct CMap {
  int &operator[](int);
};
TCFuture<int> fg_Work();
// What a lambda gives back of what it is given.
struct CApp : CActor {
  CMap m_Items;
  TCFuture<int> f_Given() {
    int *pValue = [](int &_Value) { return &_Value; }(m_Items[0]);
    co_await fg_Work();
    co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pValue' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
};
}

namespace NRound179 {
TCFuture<int> fg_Work();
struct CMap {
  int &operator[](int);
};
// Assigned through a pointer to it made with addressof.
struct CApp : CActor {
  CMap m_Items;
  TCFuture<int> f_Run() {
    int *pValue = nullptr;
    int **ppValue = __builtin_addressof(pValue);
    *ppValue = &m_Items[0];
    co_await fg_Work();
    co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pValue' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
};
}

namespace NRound187 {
// A getter of an internal object of the actor that gives back only a member
// of its own, which lives with it, unlike one of an entry.
struct CInternal : CActorInternal {
  int m_Value;
  int &f_Value() { return m_Value; }
};
struct CEntry {
  int m_Value;
  int &f_Value() { return m_Value; }
};
struct CMap {
  CEntry *f_FindEqual(int);
};
struct CMyActor : CActor {
  NMib::NStorage::TCSharedPointer<CInternal> mp_pInternal;
  CMap mp_Items;
  TCFuture<int> f_Internal() {
    int &Value = mp_pInternal->f_Value();
    co_await fg_Suspend();
    co_return Value;
  }
  TCFuture<int> f_Entry() {
    int &Value = mp_Items.f_FindEqual(1)->f_Value();
    co_await fg_Suspend();
    co_return Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'Value' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
};
}

namespace NRound190 {
// A lambda dispatched to run later, after other calls of the actor, that
// reads through a lookup it captured.
struct CValue {
  int m_Value;
};
struct CMap {
  CValue &operator[](int);
  bool f_Remove(int);
};
struct CMyActor : CActor {
  CMap mp_Items;
  TCFuture<int> f_Run() {
    auto *pValue = &mp_Items[1];
    TCFuture<int> Future = g_Dispatch / [pValue] { return pValue->m_Value; };
// CHECK-MESSAGES: :[[@LINE-1]]:42: warning: 'pValue' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
    co_return co_await fg_Move(Future);
  }
  TCFuture<int> f_Key() {
    TCFuture<int> Future = g_Dispatch / [this] { return mp_Items[1].m_Value; };
    co_return co_await fg_Move(Future);
  }
};
}

namespace NRound194 {
TCFuture<int> fg_Suspend();
// A helper given the actor by reference that returns a member of it, which
// lives with the actor.
struct CApp : CActor {
  int m_Value = 0;
  TCFuture<int> f_Run();
};
int &fg_Value(CApp &_App) { return _App.m_Value; }
TCFuture<int> CApp::f_Run() {
  int &Value = fg_Value(*this);
  co_await fg_Suspend();
  co_return Value;
}
}

namespace NRound195 {
TCFuture<int> fg_Suspend();
// A helper that gives a member of what it is given refers into an entry it is
// given, which can be erased, but not into a member of the actor.
struct CValue {
  int m_Value;
};
int &fg_Value(CValue &_Value) { return _Value.m_Value; }
template <typename t_CKey, typename t_CValue>
struct TCMap {
  t_CValue &operator[](t_CKey _Key);
};
struct CApp : CActor {
  TCMap<int, CValue> mp_Items;
  CValue m_Fixed;
  TCFuture<int> f_Entry() {
    int &Value = fg_Value(mp_Items[1]);
    co_await fg_Suspend();
    co_return Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'Value' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_Fixed() {
    int &Value = fg_Value(m_Fixed);
    co_await fg_Suspend();
    co_return Value;
  }
};
}

namespace NRound197 {
TCFuture<int> fg_Work();
// What a member function of what the actor holds can be seen to give back of
// static storage isn't in its data.
struct CInfo {
  char const *f_Category() const { return "Downloads"; }
  int const &f_Default() const {
    static int s_Value = 1;
    return s_Value;
  }
};
struct CApp : CActor {
  CInfo mp_Info;
  TCFuture<int> f_Run() {
    auto pCategory = mp_Info.f_Category();
    int const &Default = mp_Info.f_Default();
    co_await fg_Work();
    co_return pCategory[0] + Default;
  }
};
}

namespace NRound200 {
TCFuture<int> fg_Work();
struct CMap {
  int &operator[](int _Key);
  void f_Clear();
};
// Pointed at an offset into the data by a function it is given to by
// reference, also as the address of an element.
void fg_Next(int *&o_pValue, int *_pBase) { o_pValue = _pBase + 1; }
void fg_Element(int *&o_pValue, int *_pBase) { o_pValue = &_pBase[1]; }
struct CApp : CActor {
  CMap m_Items;
  TCFuture<int> f_Next() {
    int *pValue = nullptr;
    fg_Next(pValue, &m_Items[0]);
    co_await fg_Work();
    co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pValue' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_Element() {
    int *pValue = nullptr;
    fg_Element(pValue, &m_Items[0]);
    co_await fg_Work();
    co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pValue' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
};
}

namespace NRound201 {
TCFuture<int> fg_Work();
struct CStr {
  char const *f_GetStr() const;
};
// What a helper gives back of what it is given on every return, unchanged,
// is that: not what it reads of the others, but what it points elsewhere
// is not.
char *fg_StrCopy(char *o_pTo, CStr const &_From) {
  o_pTo[0] = _From.f_GetStr()[0];
  o_pTo[1] = 0;
  return o_pTo;
}
char const *fg_StrFind(char const *o_pTo, CStr const &_From) {
  o_pTo = _From.f_GetStr();
  return o_pTo;
}
struct CApp : CActor {
  CStr m_Text;
  TCFuture<int> f_Copy() {
    char Buffer[2];
    char *pText = fg_StrCopy(Buffer, m_Text);
    co_await fg_Work();
    co_return pText[0];
  }
  TCFuture<int> f_Find() {
    char Buffer[2];
    char const *pText = fg_StrFind(Buffer, m_Text);
    co_await fg_Work();
    co_return pText[0];
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pText' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
};
}

namespace NRound205 {
TCFuture<int> fg_Suspend();
// A view of the collection itself, which refers to the collection, not to
// an entry.
template <class t_CKey, class t_CValue>
struct TCMap {
  struct CKeyIterator {
    explicit operator bool() const;
    t_CKey const &operator*() const;
  };
  struct CKeys {
    CKeys(TCMap &_Map) : mp_Map(_Map) {}
    CKeyIterator f_GetIterator();

  protected:
    TCMap &mp_Map;
  };
  CKeys f_Keys() & { return *this; }
};
struct CApp : CActor {
  TCMap<int, int> mp_Items;
  TCFuture<int> f_Read() {
    auto Keys = mp_Items.f_Keys();
    co_await fg_Suspend();
    auto iKey = Keys.f_GetIterator();
    if (iKey)
      co_return *iKey;
    co_return 0;
  }
};
}

namespace NRound208
{
	// A lookup that a coroutine lambda held in a local and run by `fg_CallSafe`
	// assigns.
	struct CNamedHelper : CActor
	{
		CRound91Map m_Items;
		TCFuture<int> f_Run()
		{
			int *pValue = nullptr;
			auto fLookup = [this, &pValue]() -> TCFuture<int>
				{
					pValue = m_Items.f_FindEqual(1);
					co_return 0;
				}
			;
			co_await fg_CallSafe(fLookup);
			co_await fg_Suspend();
			co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pValue' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
		}
	};

	// One that is only stored, which runs elsewhere, doesn't.
	void fg_Store(TCFunction<TCFuture<int> ()> &&_fFunction);
	struct CNamedStored : CActor
	{
		CRound91Map m_Items;
		TCFuture<int> f_Run()
		{
			int *pValue = nullptr;
			auto fLookup = [this, &pValue]() -> TCFuture<int>
				{
					pValue = m_Items.f_FindEqual(1);
					co_return 0;
				}
			;
			fg_Store(fg_Move(fLookup));
			co_await fg_Suspend();
			co_return pValue ? *pValue : 0;
		}
	};
}

namespace NRound209 {
TCFuture<int> fg_Work();
struct CMap {
  int &operator[](int _Key);
};
// Pointed at the data by a helper that gives the address of what a reference
// refers to to another that sets it, or sets it to an unchanged local copy.
void fg_Set(int *&o_pValue, int *_pValue) { o_pValue = _pValue; }
void fg_SetRef(int *&o_pValue, int &_Value) { fg_Set(o_pValue, &_Value); }
void fg_SetCopy(int *&o_pValue, int *_pValue) {
  auto *pSaved = _pValue;
  o_pValue = pSaved;
}
struct CApp : CActor {
  CMap m_Items;
  TCFuture<int> f_SetRef() {
    int *pValue = nullptr;
    fg_SetRef(pValue, m_Items[1]);
    co_await fg_Work();
    co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pValue' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_SetCopy() {
    int *pValue = nullptr;
    fg_SetCopy(pValue, &m_Items[1]);
    co_await fg_Work();
    co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pValue' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
};
}

namespace NRound210 {
TCFuture<int> fg_Suspend();
struct CValue {
  int m_Value;
};
struct CMap {
  CValue *f_FindEqual(int _Key);
};
struct CMyActor;
TCFuture<CValue *> fg_Find(CMyActor *_pThis, int _Key);
// What a free or static coroutine gives back of what it looks up in the actor
// it is given, awaited.
struct CMyActor : CActor {
  CMap mp_Items;
  int m_Fixed;
  static TCFuture<CValue *> fs_Find(CMyActor *_pThis, int _Key) {
    co_return _pThis->mp_Items.f_FindEqual(_Key);
  }
  static TCFuture<int *> fs_Fixed(CMyActor *_pThis) {
    co_return &_pThis->m_Fixed;
  }
  TCFuture<int> f_Free() {
    auto *pValue = co_await fg_Find(this, 1);
    co_await fg_Suspend();
    co_return pValue->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pValue' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_Static() {
    auto *pValue = co_await fs_Find(this, 1);
    co_await fg_Suspend();
    co_return pValue->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pValue' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  // Not one that gives back only a member of it, which lives with it.
  TCFuture<int> f_Fixed() {
    int *pValue = co_await fs_Fixed(this);
    co_await fg_Suspend();
    co_return *pValue;
  }
  // Set by a lambda through what it is given, like a function.
  TCFuture<int> f_Lambda() {
    CValue *pValue = nullptr;
    auto fSet = [](CValue *&o_pValue, CValue *_pValue) { o_pValue = _pValue; };
    fSet(pValue, mp_Items.f_FindEqual(1));
    co_await fg_Suspend();
    co_return pValue->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pValue' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  // Got from a getter that assigns a lookup in each branch.
  CValue *f_Find(bool _bFirst) {
    CValue *pValue = nullptr;
    if (_bFirst)
      pValue = mp_Items.f_FindEqual(1);
    else
      pValue = mp_Items.f_FindEqual(2);
    return pValue;
  }
  TCFuture<int> f_Branches(bool _bFirst) {
    auto *pValue = f_Find(_bFirst);
    if (!pValue)
      co_return 0;
    co_await fg_Suspend();
    co_return pValue->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pValue' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
};
TCFuture<CValue *> fg_Find(CMyActor *_pThis, int _Key) {
  co_return _pThis->mp_Items.f_FindEqual(_Key);
}
}

namespace NRound212 {
TCFuture<int> fg_Suspend();
struct CMap {
  int *f_FindEqual(int _Key);
};
struct CApp : CActor {
  CMap mp_Items;
  static constexpr bool mc_bUseCache = false;
  // Assigned through a local reference to what it is given.
  void f_Find(int *&o_pValue) {
    int *&pOutput = o_pValue;
    pOutput = mp_Items.f_FindEqual(1);
  }
  // Not in a branch that a constant condition never takes.
  void f_UseCache(int *&o_pValue) {
    if (mc_bUseCache)
      o_pValue = mp_Items.f_FindEqual(1);
  }
  TCFuture<int> f_Alias() {
    int *pValue = nullptr;
    f_Find(pValue);
    co_await fg_Suspend();
    co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pValue' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_Constant() {
    int Value = 42;
    int *pValue = &Value;
    f_UseCache(pValue);
    co_await fg_Suspend();
    co_return *pValue;
  }
};
}

namespace NRound214 {
TCFuture<int> fg_Work();
struct CEntry {
  int m_Key;
  int m_Value;
};
struct CMap {
  CEntry *f_FindEqual(int _Key);
};
struct COnResume {
  ~COnResume();
};
struct CExceptionPointer {
  CExceptionPointer();
};
template <typename tf_FCallback>
TCFuture<COnResume> fg_OnResume(tf_FCallback &&_fCallback);
// What the assignment that looks it up again evaluates first reads the entry
// from before.
struct CApp : CActor {
  CMap mp_Items;
  TCFuture<int> f_Read(int _Key) {
    CEntry *pEntry = mp_Items.f_FindEqual(_Key);
    if (!pEntry)
      co_return 0;
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pEntry = mp_Items.f_FindEqual(pEntry->m_Key);
// CHECK-MESSAGES: :[[@LINE-1]]:37: warning: {{.*}}
      return {};
    });
    co_await fg_Work();
    co_return pEntry->m_Value;
  }
};
}

namespace NRound215 {
TCFuture<int> fg_Work();
struct CEntry {
  int m_Value;
};
struct CMap {
  CEntry *f_FindEqual(int _Key);
};
struct CStr {
  int f_GetLen() const;
};
// What a member function gives of a lookup in what it is given by reference.
struct CMyActor : CActor {
  CMap mp_Items;
  CStr mp_Text;
  CEntry *f_Find(CMap &_Items) { return _Items.f_FindEqual(1); }
  TCFuture<int> f_Run() {
    auto *pEntry = f_Find(mp_Items);
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  // Not what a free function gives that is only static, either of a
  // conditional.
  TCFuture<int> f_Status();
};
char const *fg_Status(CStr const &_Text) {
  return _Text.f_GetLen() ? "Present" : "Empty";
}
TCFuture<int> CMyActor::f_Status() {
  char const *pStatus = fg_Status(mp_Text);
  co_await fg_Work();
  co_return *pStatus;
}
}

namespace NRound216 {
TCFuture<int> fg_Work();
struct COnResume {
  ~COnResume();
};
struct CExceptionPointer {
  CExceptionPointer();
};
CExceptionPointer fg_MakeError(char const *_pMessage);
template <typename tf_FCallback>
TCFuture<COnResume> fg_OnResume(tf_FCallback &&_fCallback);
template <typename t_CValue>
struct TCVector {
  t_CValue &operator[](int _Index);
  t_CValue &f_Insert(t_CValue const &_Value);
};
struct CValue {
  int m_Value;
};
struct CMap {
  CValue *f_FindEqual(int _Key);
  CValue &operator[](int _Key);
};
// A helper that moves the entries of what it is given, after it is looked up
// again.
void fg_Add(TCVector<int> &_Items) { _Items.f_Insert(42); }
struct CApp : CActor {
  TCVector<int> mp_Items;
  CMap mp_Map;
  TCFuture<int> f_Relocated() {
    int *pValue = &mp_Items[0];
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pValue = &mp_Items[0];
      fg_Add(mp_Items);
      return {};
    });
    co_await fg_Work();
    co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: {{.*}}
  }
  // Looked up again in the initializer of an `if`, which runs each time.
  TCFuture<int> f_IfInit(int _Key) {
    CValue *pValue = nullptr;
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      if (pValue = mp_Map.f_FindEqual(_Key); !pValue)
        return fg_MakeError("Entry removed");
      return {};
    });
    co_await fg_Work();
    co_return pValue->m_Value;
  }
  // Cleared before it is looked up again.
  TCFuture<int> f_Cleared(int _Key) {
    auto *pValue = mp_Map.f_FindEqual(_Key);
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pValue = nullptr;
      pValue = mp_Map.f_FindEqual(_Key);
      if (!pValue)
        return fg_MakeError("Removed");
      return {};
    });
    co_await fg_Work();
    co_return pValue->m_Value;
  }
  // Both outputs of a helper, the second made of the first it looked up
  // again.
  void f_Lookup(int _Key, CValue *&o_pValue, int *&o_pMember) {
    o_pValue = &mp_Map[_Key];
    o_pMember = &o_pValue->m_Value;
  }
  TCFuture<int> f_Derived(int _Key) {
    auto *pValue = &mp_Map[_Key];
    auto *pMember = &pValue->m_Value;
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      f_Lookup(_Key, pValue, pMember);
      return {};
    });
    co_await fg_Work();
    co_return *pMember;
  }
};
}

namespace NRound217 {
TCFuture<int> fg_Work();
struct COnResume {
  ~COnResume();
};
struct CExceptionPointer {
  CExceptionPointer();
};
CExceptionPointer fg_MakeError(char const *_pMessage);
template <typename tf_FCallback>
TCFuture<COnResume> fg_OnResume(tf_FCallback &&_fCallback);
template <typename t_CValue>
struct TCVector {
  t_CValue &operator[](int _Index);
  int f_GetLen() const;
  void f_SetLen(int _Len);
  t_CValue *f_GetArray();
  // Resizes it first, which can move the entries.
  t_CValue *f_GetArray(int _Len) {
    f_SetLen(_Len);
    return f_GetArray();
  }
};
struct CValue {
  int m_Value;
};
struct CMap {
  CValue *f_FindEqual(int _Key);
  void f_Remove(int _Key);
};
struct CScopeExit {
  ~CScopeExit();
  void f_Clear();
};
struct COnScopeExitHelper {};
extern COnScopeExitHelper const &g_OnScopeExit;
template <typename tf_FCleanup>
CScopeExit operator/(COnScopeExitHelper const &, tf_FCleanup &&_fCleanup);
// Calls what it is given before it returns.
template <typename tf_FRead>
int fg_Read(tf_FRead &&_fRead) {
  return _fRead();
}
// Only keeps what it is given.
template <typename tf_FRead>
int fg_Keep(tf_FRead &&_fRead);
struct CApp : CActor {
  CMap mp_Items;
  TCVector<int> mp_Values;
  // A helper that calls the lambda reads what it captures.
  TCFuture<int> f_ReadHelper() {
    auto *pEntry = mp_Items.f_FindEqual(1);
    co_await fg_Work();
    co_return fg_Read([pEntry] { return pEntry->m_Value; });
// CHECK-MESSAGES: :[[@LINE-1]]:24: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_ReadGenericHelper() {
    auto *pEntry = mp_Items.f_FindEqual(1);
    co_await fg_Work();
    co_return [](auto _fRead) { return _fRead(); }(
        [pEntry] { return pEntry->m_Value; });
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_KeepHelper() {
    auto *pEntry = mp_Items.f_FindEqual(1);
    co_await fg_Work();
    co_return fg_Keep([pEntry] { return 1; });
  }
  // What a helper that may not call it assigns isn't sure to have been
  // looked up again.
  TCFuture<int> f_RefreshHelper() {
    auto *pEntry = mp_Items.f_FindEqual(1);
    co_await fg_Work();
    fg_Read([&] {
      pEntry = mp_Items.f_FindEqual(1);
      return 0;
    });
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  // Testing the pointer explicitly doesn't use the entry.
  TCFuture<int> f_HadEntry() {
    auto *pEntry = mp_Items.f_FindEqual(1);
    co_await fg_Work();
    co_return bool(pEntry);
  }
  TCFuture<int> f_HadEntryCast() {
    auto *pEntry = mp_Items.f_FindEqual(1);
    co_await fg_Work();
    co_return static_cast<bool>(pEntry) ? 1 : 0;
  }
  TCFuture<int> f_HadEntryInt() {
    auto *pEntry = mp_Items.f_FindEqual(1);
    co_await fg_Work();
    co_return static_cast<int>(pEntry->m_Value);
// CHECK-MESSAGES: :[[@LINE-1]]:32: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  // Resizing through a member function that resizes moves the entries.
  TCFuture<int> f_Resized() {
    int *pValue = &mp_Values[0];
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pValue = &mp_Values[0];
      mp_Values.f_GetArray(mp_Values.f_GetLen() + 1024);
      return {};
    });
    co_await fg_Work();
    co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pValue' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_NotResized() {
    int *pValue = &mp_Values[0];
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pValue = &mp_Values[0];
      mp_Values.f_GetArray();
      return {};
    });
    co_await fg_Work();
    co_return *pValue;
  }
  // A rollback that is cancelled before the callback can succeed removes
  // nothing the coroutine reads.
  TCFuture<int> f_RolledBack() {
    CValue *pValue = nullptr;
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      auto Rollback = g_OnScopeExit / [&] { mp_Items.f_Remove(1); };
      pValue = mp_Items.f_FindEqual(1);
      if (!pValue)
        return fg_MakeError("Removed");
      Rollback.f_Clear();
      return {};
    });
    co_await fg_Work();
    co_return pValue->m_Value;
  }
  // But not one it can succeed before cancelling.
  TCFuture<int> f_NotRolledBack(bool _bEarly) {
    CValue *pValue = nullptr;
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      auto Rollback = g_OnScopeExit / [&] { mp_Items.f_Remove(1); };
      pValue = mp_Items.f_FindEqual(1);
      if (!pValue)
        return fg_MakeError("Removed");
      if (_bEarly)
        return {};
      Rollback.f_Clear();
      return {};
    });
    co_await fg_Work();
    co_return pValue->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pValue' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
};
}

namespace NRound228 {
TCFuture<int> fg_Suspend();
struct CValue {
  int m_Value;
};
struct CMap {
  CValue *f_FindEqual(int _Key);
};
struct CMyActor;
TCFuture<char const *> fg_ReadStatus(CMyActor *_pThis);
// What a coroutine looks up, awaited with a context for its error, and not
// what a free one gives back of static storage.
struct CMyActor : CActor {
  CMap mp_Items;
  TCFuture<CValue *> fp_Find(int _Key) { co_return mp_Items.f_FindEqual(_Key); }
  TCFuture<int> f_Context() {
    auto *pValue = co_await (fp_Find(1) % "Finding entry");
    co_await fg_Suspend();
    co_return pValue->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pValue' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_Status() {
    char const *pStatus = co_await fg_ReadStatus(this);
    co_await fg_Suspend();
    co_return pStatus[0];
  }
};
TCFuture<char const *> fg_ReadStatus(CMyActor *_pThis) {
  co_await fg_Suspend();
  co_return "Ready";
}
}

namespace NRound229 {
TCFuture<int> fg_Suspend();
struct CValue {
  int m_Value;
};
struct CMap {
  CValue *f_FindEqual(int _Key);
};
// What a local lambda looks up in the actor it is given a pointer to, not
// only a member of it.
struct CMyActor : CActor {
  CMap mp_Items;
  int m_Fixed = 0;
  TCFuture<int> f_Pointer() {
    auto fFind = [](CMyActor *_pThis) { return _pThis->mp_Items.f_FindEqual(1); };
    auto *pValue = fFind(this);
    co_await fg_Suspend();
    co_return pValue->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pValue' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_Member() {
    auto fFixed = [](CMyActor *_pThis) { return &_pThis->m_Fixed; };
    int *pValue = fFixed(this);
    co_await fg_Suspend();
    co_return *pValue;
  }
};
}

namespace NRound230
{
	// What an inline lookup gives, used after a suspension in the same
	// expression, like through a local pointer, but not one looked up after.
	template <typename t_CValue>
	struct TCArray
	{
		t_CValue *f_GetArray();
	};
	TCFuture<int> fg_Index();

	struct CApp : CActor
	{
		TCArray<int> mp_Values;
		TCSharedPointer<int> mp_pValue;

		TCFuture<int> f_Inline()
		{
			co_return *(mp_Values.f_GetArray() + co_await fg_Index());
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: the data of the actor that this expression uses is looked up before it suspends, during which other calls of the actor can change or erase the data; look it up after the suspension [malterlib-coroutine-actor-reference]
		}

		TCFuture<int> f_After()
		{
			co_return *(co_await fg_Index() + mp_Values.f_GetArray());
		}

		int &f_Value()
		{
			return *mp_pValue;
		}

		TCFuture<int> f_ThroughAlias()
		{
			CApp &This = *this;
			int &Value = This.f_Value();
			co_await fg_Index();
			co_return Value;
		}
	};

	// Also through what an internal names for the object of the actor.
	struct CInternal : CActorInternal
	{
		CApp *m_pThis;

		TCFuture<int> f_Run()
		{
			int &Value = m_pThis->f_Value();
			co_await fg_Index();
			co_return Value;
		}
	};
}

namespace NRound234Dispatch {
TCFuture<int> fg_Work();
template <typename t_CType>
struct TCAsyncResult {};
// Gives the result of the future to a callback queued on the actor, like
// `fg_Work() > fg_ThisActor(this) / [...](TCAsyncResult<int> &&) {...}`.
template <typename t_CResult>
void operator>(TCFuture<int> &&_Future, t_CResult &&_Result);
template <typename tf_FToDispatch, typename... tfp_CParams>
TCFuture<int> fg_DirectDispatch(tf_FToDispatch &&_fDispatch,
                                tfp_CParams &&...p_Params);
void fg_Read(int);
struct CEntry {
  int m_Value;
};
struct CMap {
  CEntry *f_FindEqual(int);
};
struct CApp : CActor {
  CMap mp_Items;
  // The callback runs after other calls of the actor.
  TCFuture<int> f_Process() {
    CEntry *pEntry = mp_Items.f_FindEqual(1);
    fg_Work() > fg_ThisActor(this) / [pEntry](TCAsyncResult<int> &&_Result) {
// CHECK-MESSAGES: :[[@LINE-1]]:39: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
      fg_Read(pEntry->m_Value);
    };
    co_return 0;
  }
  // A direct dispatch runs it here, until it suspends.
  TCFuture<int> f_Direct() {
    CEntry *pEntry = mp_Items.f_FindEqual(1);
    co_return co_await fg_DirectDispatch(
        [pEntry]() -> TCFuture<int> { co_return pEntry->m_Value; });
  }
};
}
namespace NRound234Direct {
TCFuture<int> fg_Work();
template <typename tf_FToDispatch, typename... tfp_CParams>
TCFuture<int> fg_DirectDispatch(tf_FToDispatch &&_fDispatch,
                                tfp_CParams &&...p_Params);
struct CEntry {
  int m_Value;
};
struct CMap {
  CEntry *f_FindEqual(int);
};
struct CApp : CActor {
  CMap mp_Items;
  // What it reads after it suspends can be gone.
  TCFuture<int> f_Suspends() {
    CEntry *pEntry = mp_Items.f_FindEqual(1);
    co_return co_await fg_DirectDispatch([pEntry]() -> TCFuture<int> {
      co_await fg_Work();
      co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:17: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
    });
  }
};
}
namespace NRound234Replace {
TCFuture<int> fg_Work();
template <typename t_CValue>
struct TCVector {
  t_CValue &operator[](int _iIndex);
  int f_GetLen() const;
  template <typename tf_CSource>
  void f_Replace(tf_CSource &&_Source, int _iStart, int _nRemove, int _iFrom,
                 int _nAdd);
};
struct CApp : CActor {
  TCVector<int> mp_Values;
  TCVector<int> mp_Replacements;
  // Replacing the elements after refreshing it can free what it points to.
  TCFuture<int> f_Read() {
    int *pValue = &mp_Values[0];
    auto OnResume = co_await fg_OnResume([&]() -> CExceptionPointer {
      pValue = &mp_Values[0];
      mp_Values.f_Replace(mp_Replacements, 0, 1, 0, mp_Values.f_GetLen() + 1);
      return nullptr;
    });
    co_await fg_Work();
    co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'pValue' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
};
}
namespace NRound234Internal {
TCFuture<int> fg_Work();
struct CInternal {
  int m_Value;
};
struct CApp : CActor {
  TCSharedPointer<CInternal> mp_pInternal;
  TCFuture<int> f_Run();
};
// What an owning member points to lives with the actor.
CInternal &fg_Internal(CApp *_pThis) { return *_pThis->mp_pInternal; }
TCFuture<int> CApp::f_Run() {
  auto &Internal = fg_Internal(this);
  co_await fg_Work();
  co_return Internal.m_Value;
}
}
namespace NRound235Optional {
TCFuture<int> fg_Work();
struct CValue {
  int m_Value;
  ~CValue();
};
template <typename t_CType>
struct TCOptional {
  t_CType &operator*();
  void f_Clear();
};
struct CMyActor : CActor {
  TCOptional<CValue> mp_Value;
  // What an optional member holds can be cleared.
  static CValue &fs_Value(CMyActor &_Actor) { return *_Actor.mp_Value; }
  TCFuture<int> f_Process() {
    auto &Value = fs_Value(*this);
    co_await fg_Work();
    co_return Value.m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'Value' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
};
}
namespace NRound243 {
TCFuture<int> fg_Work();
CValue *fg_Find(CMap &_Items, int _Key) { return _Items.f_FindEqual(_Key); }
// A lookup through a function object or a function pointer given the data.
struct CApp : CActor {
  CMap mp_Items;
  TCFuture<int> f_Function() {
    NMib::NFunction::TCFunction<CValue *(CMap &, int)> fFind = fg_Find;
    auto *pEntry = fFind(mp_Items, 1);
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
  TCFuture<int> f_Pointer() {
    CValue *(*const pFind)(CMap &, int) = fg_Find;
    auto *pEntry = pFind(mp_Items, 1);
    co_await fg_Work();
    co_return pEntry->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
  }
};
}
namespace NRound245 {
TCFuture<int> fg_Work();
// A lambda that reads an entry, called through an unchanged local reference to
// a function wrapper after a suspension.
struct CApp : CActor {
  CMap mp_Items;
  TCFuture<int> f_Read() {
    auto *pEntry = mp_Items.f_FindEqual(1);
    NMib::NFunction::TCFunction<int ()> fRead = [pEntry] { return pEntry->m_Value; };
// CHECK-MESSAGES: :[[@LINE-1]]:50: warning: 'pEntry' refers into the data of the actor and is used after a suspension, during which other calls of the actor can change or erase the data; look it up again, or re-validate it with fg_OnResume [malterlib-coroutine-actor-reference]
    auto &fAlias = fRead;
    co_await fg_Work();
    co_return fAlias();
  }
};
}
