// RUN: %check_clang_tidy -std=c++20-or-later %s malterlib-async-destroy %t -- -config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" -- -I %S/Inputs

#include "malterlib-concurrency.h"

struct CTarget
{
	TCFuture<int> f_Destroy();
	int m_Value = 0;
};

struct CSyncTarget
{
	void f_Destroy();
};

void fg_Store(CAsyncDestroy &&_Destroy);
TCFuture<int> fg_StoreElsewhere(CAsyncDestroy &&_Destroy);
TCFuture<int> fg_Suspend();

TCFuture<int> fg_Destroy()
{
	CTarget Target;

	auto Cleanup = co_await fg_AsyncDestroy
		(
			[&Target]() -> TCFuture<int>
			{
				co_await ECoroutineFlag_CaptureExceptions;
				ECoroutineFlag Flags = ECoroutineFlag_CaptureExceptions;
				co_await Flags;
				auto Moved = co_await fg_MoveThis(Target);
				CTarget Copy = Target;
				co_await Copy.f_Destroy();
				co_await Target.f_Destroy();
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: the cleanup function of 'fg_AsyncDestroy' uses 'Target', which it captures by reference, after its first suspension, where the reference is gone; copy or move it into a local before the suspension [malterlib-async-destroy]
				co_return 0;
			}
		)
	;

	CTarget Unreachable;
	auto UnreachableCleanup = co_await fg_AsyncDestroyByValue
		(
			[&Unreachable]() -> TCFuture<int>
			{
				if (false)
					co_await Unreachable.f_Destroy();
				CTarget Copy = Unreachable;
				co_return 0;
			}
		)
	;

	CTarget Aliased;
	auto AliasCleanup = co_await fg_AsyncDestroy
		(
			[&Aliased]() -> TCFuture<int>
			{
				auto &Alias = Aliased;
				co_await fg_Suspend();
				co_await Alias.f_Destroy();
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: the cleanup function of 'fg_AsyncDestroy' uses 'Alias', which it captures by reference, after its first suspension, where the reference is gone; copy or move it into a local before the suspension [malterlib-async-destroy]
				co_return 0;
			}
		)
	;

	CTarget Member;
	auto MemberCleanup = co_await fg_AsyncDestroy
		(
			[&Member]() -> TCFuture<int>
			{
				auto &Sub = Member.m_Value;
				co_await fg_Suspend();
				co_return Sub;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: the cleanup function of 'fg_AsyncDestroy' uses 'Sub', which it captures by reference, after its first suspension, where the reference is gone; copy or move it into a local before the suspension [malterlib-async-destroy]
			}
		)
	;

	int *pStatic = nullptr;
	auto CopyCleanup = co_await fg_AsyncDestroy
		(
			[&pStatic]() -> TCFuture<int>
			{
				int *pCopy = pStatic;
				int &Pointee = *pStatic;
				co_await fg_Suspend();
				co_return *pCopy + Pointee;
			}
		)
	;

	CTarget Pointed;
	auto PointerCleanup = co_await fg_AsyncDestroy
		(
			[&Pointed]() -> TCFuture<int>
			{
				auto *pPointed = &Pointed;
				auto *pAlias = pPointed;
				co_await fg_Suspend();
				co_return pAlias->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: the cleanup function of 'fg_AsyncDestroy' uses 'pAlias', which it captures by reference, after its first suspension, where the reference is gone; copy or move it into a local before the suspension [malterlib-async-destroy]
			}
		)
	;

	CTarget Looped2;
	auto LoopAliasCleanup = co_await fg_AsyncDestroy
		(
			[&Looped2]() -> TCFuture<int>
			{
				// The copy comes before the pointer it copies in the source.
				CTarget *pFirst = nullptr;
				CTarget *pSecond = nullptr;
				for (int iTry = 0; iTry < 2; ++iTry)
				{
					pSecond = pFirst;
					pFirst = &Looped2;
				}
				co_await fg_Suspend();
				co_return pSecond ? pSecond->m_Value : 0;
// CHECK-MESSAGES: :[[@LINE-1]]:25: warning: the cleanup function of 'fg_AsyncDestroy' uses 'pSecond', which it captures by reference, after its first suspension, where the reference is gone; copy or move it into a local before the suspension [malterlib-async-destroy]
			}
		)
	;

	CTarget Invoked;
	auto InvokedCleanup = co_await fg_AsyncDestroy
		(
			[&Invoked]() -> TCFuture<int>
			{
				auto fRead = [&Invoked] { return Invoked.m_Value; };
				co_await fg_Suspend();
				co_return fRead();
// CHECK-MESSAGES: :[[@LINE-3]]:20: warning: the cleanup function of 'fg_AsyncDestroy' uses 'Invoked', which it captures by reference, after its first suspension, where the reference is gone; copy or move it into a local before the suspension [malterlib-async-destroy]
			}
		)
	;

	CTarget Conditional;
	auto ConditionalCleanup = co_await fg_AsyncDestroy
		(
			[&Conditional](bool _bUse) -> TCFuture<int>
			{
				CTarget Local;
				CTarget *pTarget = _bUse ? &Conditional : &Local;
				co_await fg_Suspend();
				co_return pTarget->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: the cleanup function of 'fg_AsyncDestroy' uses 'pTarget', which it captures by reference, after its first suspension, where the reference is gone; copy or move it into a local before the suspension [malterlib-async-destroy]
			}
		)
	;

	CTarget CapturedPointer;
	auto CapturedPointerCleanup = co_await fg_AsyncDestroy
		(
			[&CapturedPointer]() -> TCFuture<int>
			{
				CTarget *pTarget = &CapturedPointer;
				auto fRead = [pTarget] { return pTarget->m_Value; };
				co_await fg_Suspend();
				co_return fRead();
// CHECK-MESSAGES: :[[@LINE-3]]:19: warning: the cleanup function of 'fg_AsyncDestroy' uses 'pTarget', which it captures by reference, after its first suspension, where the reference is gone; copy or move it into a local before the suspension [malterlib-async-destroy]
			}
		)
	;

	CTarget Snapshot;
	auto SnapshotCleanup = co_await fg_AsyncDestroy
		(
			[&Snapshot]() -> TCFuture<int>
			{
				// The lambda keeps its copy when the pointer is redirected.
				CTarget Local;
				CTarget *pTarget = &Snapshot;
				auto fRead = [pTarget] { return pTarget->m_Value; };
				pTarget = &Local;
				co_await fg_Suspend();
				co_return fRead() + pTarget->m_Value;
// CHECK-MESSAGES: :[[@LINE-4]]:19: warning: the cleanup function of 'fg_AsyncDestroy' uses 'pTarget', which it captures by reference, after its first suspension, where the reference is gone; copy or move it into a local before the suspension [malterlib-async-destroy]
			}
		)
	;

	CTarget ReferencedPointer;
	auto ReferencedPointerCleanup = co_await fg_AsyncDestroy
		(
			[&ReferencedPointer]() -> TCFuture<int>
			{
				CTarget *pTarget = &ReferencedPointer;
				auto fRead = [&pTarget] { return pTarget->m_Value; };
				co_await fg_Suspend();
				co_return fRead();
// CHECK-MESSAGES: :[[@LINE-3]]:20: warning: the cleanup function of 'fg_AsyncDestroy' uses 'pTarget', which it captures by reference, after its first suspension, where the reference is gone; copy or move it into a local before the suspension [malterlib-async-destroy]
			}
		)
	;

	CTarget InitCaptured;
	auto InitCapturedCleanup = co_await fg_AsyncDestroy
		(
			[&InitCaptured](bool _bChoose) -> TCFuture<int>
			{
				auto fRead = [pTarget = &InitCaptured] { return pTarget->m_Value; };
				CTarget Local;
				CTarget &Alias = _bChoose ? InitCaptured : Local;
				co_await fg_Suspend();
				co_return fRead() + Alias.m_Value;
// CHECK-MESSAGES: :[[@LINE-5]]:19: warning: the cleanup function of 'fg_AsyncDestroy' uses 'pTarget', which it captures by reference, after its first suspension, where the reference is gone; copy or move it into a local before the suspension [malterlib-async-destroy]
// CHECK-MESSAGES: :[[@LINE-2]]:25: warning: the cleanup function of 'fg_AsyncDestroy' uses 'Alias', which it captures by reference, after its first suspension, where the reference is gone; copy or move it into a local before the suspension [malterlib-async-destroy]
			}
		)
	;

	using CScopeExitType = decltype(g_OnScopeExit / [&Snapshot]() { Snapshot.f_Destroy(); });

	CTarget Thrown;
	auto ThrowCleanup = co_await fg_AsyncDestroy
		(
			[&Thrown]() -> TCFuture<int>
			{
				try
				{
					co_await CThrowingAwaiter{};
				}
				catch (...)
				{
					co_return Thrown.m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: the cleanup function of 'fg_AsyncDestroy' uses 'Thrown', which it captures by reference, after its first suspension, where the reference is gone; copy or move it into a local before the suspension [malterlib-async-destroy]
				}
				co_return 0;
			}
		)
	;

	CTarget Reassigned;
	auto ReassignCleanup = co_await fg_AsyncDestroy
		(
			[&Reassigned]() -> TCFuture<int>
			{
				CTarget Local;
				auto *pTarget = &Reassigned;
				pTarget = &Local;
				co_await fg_Suspend();
				co_return pTarget->m_Value;
			}
		)
	;

	CTarget AssignedLater;
	auto LaterCleanup = co_await fg_AsyncDestroy
		(
			[&AssignedLater]() -> TCFuture<int>
			{
				CTarget *pTarget = nullptr;
				pTarget = &AssignedLater;
				co_await fg_Suspend();
				co_return pTarget->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: the cleanup function of 'fg_AsyncDestroy' uses 'pTarget', which it captures by reference, after its first suspension, where the reference is gone; copy or move it into a local before the suspension [malterlib-async-destroy]
			}
		)
	;

	int Values[2] = {};
	auto ArithmeticCleanup = co_await fg_AsyncDestroy
		(
			[&Values]() -> TCFuture<int>
			{
				auto *pSecond = Values + 1;
				co_await fg_Suspend();
				co_return *pSecond;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: the cleanup function of 'fg_AsyncDestroy' uses 'pSecond', which it captures by reference, after its first suspension, where the reference is gone; copy or move it into a local before the suspension [malterlib-async-destroy]
			}
		)
	;

	CTarget Rebound;
	auto ReboundCleanup = co_await fg_AsyncDestroy
		(
			[&Rebound]() -> TCFuture<int>
			{
				CTarget Independent;
				auto *pTarget = &Rebound;
				pTarget = &Independent;
				auto &Bound = *pTarget;
				co_await fg_Suspend();
				co_return Bound.m_Value;
			}
		)
	;

	CTarget MovedAlias;
	auto MovedAliasCleanup = co_await fg_AsyncDestroy
		(
			[&MovedAlias]() -> TCFuture<int>
			{
				auto &&Alias = fg_Move(MovedAlias);
				co_await fg_Suspend();
				co_return Alias.m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: the cleanup function of 'fg_AsyncDestroy' uses 'Alias', which it captures by reference, after its first suspension, where the reference is gone; copy or move it into a local before the suspension [malterlib-async-destroy]
			}
		)
	;

	CTarget Named;
	auto fNamedCleanup = [&Named]() -> TCFuture<int>
		{
			co_await fg_Suspend();
			co_await Named.f_Destroy();
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: the cleanup function of 'fg_AsyncDestroy' uses 'Named', which it captures by reference, after its first suspension, where the reference is gone; copy or move it into a local before the suspension [malterlib-async-destroy]
			co_return 0;
		}
	;
	auto NamedCleanup = co_await fg_AsyncDestroy(fg_Move(fNamedCleanup));

	using CAwaiterType = decltype(fg_AsyncDestroy([]() -> TCFuture<int> { co_return 0; }));

	CTarget Looped;
	auto LoopCleanup = co_await fg_AsyncDestroy
		(
			[&Looped]() -> TCFuture<int>
			{
				for (int iTry = 0; iTry < 2; ++iTry)
					co_await Looped.f_Destroy();
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: the cleanup function of 'fg_AsyncDestroy' uses 'Looped', which it captures by reference, after its first suspension, where the reference is gone; copy or move it into a local before the suspension [malterlib-async-destroy]
				co_return 0;
			}
		)
	;

	co_await fg_AsyncDestroy([]() -> TCFuture<int> { co_return 0; });
// CHECK-MESSAGES: :[[@LINE-1]]:2: warning: the result of co_awaiting 'fg_AsyncDestroy' is not kept in a named variable, so the cleanup runs at the end of the statement instead of at the end of the scope [malterlib-async-destroy]

	auto Awaiter = fg_AsyncDestroy([]() -> TCFuture<int> { co_return 0; });
// CHECK-MESSAGES: :[[@LINE-1]]:17: warning: 'fg_AsyncDestroy' schedules the cleanup when it is co_awaited; co_await it and keep the result in a named variable [malterlib-async-destroy]

	auto Moved = co_await fg_AsyncDestroy([]() -> TCFuture<int> { co_return 0; });
	fg_Store(fg_Move(Moved));
// CHECK-MESSAGES: :[[@LINE-1]]:11: warning: the result of 'fg_AsyncDestroy' is moved, so the cleanup can run after the locals it refers to are gone; keep it in its variable [malterlib-async-destroy]

	// Also cast to an rvalue.
	auto Cast = co_await fg_AsyncDestroy([]() -> TCFuture<int> { co_return 0; });
	fg_Store(static_cast<decltype(Cast) &&>(Cast));
// CHECK-MESSAGES: :[[@LINE-1]]:11: warning: the result of 'fg_AsyncDestroy' is moved, so the cleanup can run after the locals it refers to are gone; keep it in its variable [malterlib-async-destroy]

	// Awaiting the result runs the cleanup early, on purpose.
	auto Early = co_await fg_AsyncDestroy([]() -> TCFuture<int> { co_return 0; });
	co_await fg_Move(Early);

	// Awaiting something the result is passed to does not run the cleanup.
	auto Passed = co_await fg_AsyncDestroy([]() -> TCFuture<int> { co_return 0; });
	co_await fg_StoreElsewhere(fg_Move(Passed));
// CHECK-MESSAGES: :[[@LINE-1]]:29: warning: the result of 'fg_AsyncDestroy' is moved, so the cleanup can run after the locals it refers to are gone; keep it in its variable [malterlib-async-destroy]

	auto ScopeExit = g_OnScopeExit / [&Target]()
// CHECK-MESSAGES: :[[@LINE-1]]:19: warning: the teardown of an actor or functor in a coroutine uses 'fg_AsyncDestroy' instead of 'g_OnScopeExit' [malterlib-async-destroy]
		{
			Target.f_Destroy();
		}
	;

	co_return 0;
}

CAsyncDestroyAwaiter fg_Factory(CTarget &_Target)
{
	// The caller awaits it, but the cleanup is still checked.
	return fg_AsyncDestroy
		(
			[&_Target]() -> TCFuture<int>
			{
				co_await fg_Suspend();
				co_await _Target.f_Destroy();
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: the cleanup function of 'fg_AsyncDestroy' uses '_Target', which it captures by reference, after its first suspension, where the reference is gone; copy or move it into a local before the suspension [malterlib-async-destroy]
				co_return 0;
			}
		)
	;
}

TCFuture<int> fg_SyncTeardown()
{
	CSyncTarget Target;
	auto ScopeExit = g_OnScopeExit / [&Target]()
		{
			Target.f_Destroy();
		}
	;
	co_return 0;
}

void fg_NotCoroutine()
{
	CTarget Target;
	auto ScopeExit = g_OnScopeExit / [&Target]()
		{
			Target.f_Destroy();
		}
	;
}

TCFuture<CAsyncDestroy> fg_ReturnsCleanup()
{
	auto Cleanup = co_await fg_AsyncDestroy([]() -> TCFuture<int> { co_return 0; });
	co_return Cleanup;
// CHECK-MESSAGES: :[[@LINE-1]]:12: warning: the result of 'fg_AsyncDestroy' is returned, so the cleanup can run after the locals it refers to are gone; keep it in its variable [malterlib-async-destroy]
}

template <typename t_CType>
TCFuture<int> fg_TemplateStore(t_CType _Value)
{
	// The cleanup depends on the template, and so does its move.
	auto Cleanup = co_await fg_AsyncDestroy([_Value]() -> TCFuture<int> { co_return 0; });
	fg_Store(fg_Move(Cleanup));
// CHECK-MESSAGES: :[[@LINE-1]]:11: warning: the result of 'fg_AsyncDestroy' is moved, so the cleanup can run after the locals it refers to are gone; keep it in its variable [malterlib-async-destroy]
	co_return 0;
}
template TCFuture<int> fg_TemplateStore<int>(int _Value);

template <typename t_CType>
TCFuture<int> fg_TemplateEarly(t_CType _Value)
{
	// Awaiting it through f_Wrap() runs the cleanup early, also in a template.
	auto Cleanup = co_await fg_AsyncDestroy([_Value]() -> TCFuture<int> { co_return 0; });
	co_await fg_Move(Cleanup).f_Wrap();
	co_return 0;
}

// In code that runs to completion, the cleanup ends before the coroutine, while
// its parameters are there.
#define DMibEdgeRunsToCompletion [[clang::annotate("malterlib-runs-to-completion")]]
DMibEdgeRunsToCompletion TCFuture<int> fg_RunsToCompletionCleanup(int _Value)
{
	auto Cleanup = co_await fg_AsyncDestroy
		(
			[&_Value]() -> TCFuture<int>
			{
				co_await fg_Suspend();
				co_return _Value;
			}
		)
	;
	co_return 0;
}

// A guard chosen by a conditional is kept, and one moved through a reference
// to it escapes.
TCFuture<int> fg_ChosenGuard(bool _bLeft, TCActor<> _Left, TCActor<> _Right)
{
	auto Cleanup = _bLeft ? co_await fg_AsyncDestroy(_Left) : co_await fg_AsyncDestroy(_Right);
	co_return 0;
}
template <typename tf_CValue>
void fg_StoreGuard(tf_CValue &&_Value);
TCFuture<int> fg_MovedThroughAlias(TCActor<> _Target)
{
	auto Cleanup = co_await fg_AsyncDestroy(_Target);
	auto &Alias = Cleanup;
	fg_StoreGuard(fg_Move(Alias));
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: the result of 'fg_AsyncDestroy' is moved, so the cleanup can run after the locals it refers to are gone; keep it in its variable [malterlib-async-destroy]
	co_return 0;
}

// A pointer that the cleanup reads, pointed at a local that is destroyed
// before the cleanup runs, and one moved through a reference to a reference.
struct CPointedValue
{
	int m_Value = 42;
	~CPointedValue();
};
TCFuture<int> fg_PointedAtShorterLived()
{
	CPointedValue Early;
	CPointedValue *pValue = &Early;
	auto Cleanup = co_await fg_AsyncDestroy
		(
			[&pValue]() -> TCFuture<int>
			{
				co_return pValue->m_Value;
			}
		)
	;
	CPointedValue Local;
	pValue = &Local;
// CHECK-MESSAGES: :[[@LINE-1]]:2: warning: 'pValue', which the cleanup of 'fg_AsyncDestroy' uses, is pointed at 'Local', which is destroyed before the cleanup runs; point it at what is declared before the result of 'fg_AsyncDestroy' [malterlib-async-destroy]
	pValue = &Early;
	co_return 0;
}
TCFuture<int> fg_MovedThroughAliases(TCActor<> _Target)
{
	auto Cleanup = co_await fg_AsyncDestroy(_Target);
	auto &Alias = Cleanup;
	auto &Alias2 = Alias;
	fg_StoreGuard(fg_Move(Alias2));
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: the result of 'fg_AsyncDestroy' is moved, so the cleanup can run after the locals it refers to are gone; keep it in its variable [malterlib-async-destroy]
	co_return 0;
}
TCFuture<int> fg_PointedAfterCleanup()
{
	int Original = 0;
	int *pValue = &Original;
	auto Cleanup = co_await fg_AsyncDestroy
		(
			[&pValue]() -> TCFuture<int>
			{
				co_return *pValue;
			}
		)
	;
	co_await fg_Move(Cleanup);
	int Later = 1;
	pValue = &Later;
	co_return 0;
}
TCFuture<int> fg_PointedAfterMaybeCancelled(bool _bCancel)
{
	int *pValue = nullptr;
	auto Cleanup = co_await fg_AsyncDestroy
		(
			[&pValue]() -> TCFuture<int>
			{
				co_return *pValue;
			}
		)
	;
	if (_bCancel)
		Cleanup.f_Clear();
	int Local = 42;
	pValue = &Local;
// CHECK-MESSAGES: :[[@LINE-1]]:2: warning: 'pValue', which the cleanup of 'fg_AsyncDestroy' uses, is pointed at 'Local', {{.*}}
	co_return 0;
}
TCFuture<int> fg_PointedAfterAliasAwaited()
{
	int *pValue = nullptr;
	auto Cleanup = co_await fg_AsyncDestroy
		(
			[&pValue]() -> TCFuture<int>
			{
				co_return pValue ? *pValue : 0;
			}
		)
	;
	auto &Alias = Cleanup;
	co_await fg_Move(Alias);
	int Local = 42;
	pValue = &Local;
	co_return 0;
}
TCFuture<int> fg_PointedThroughReference()
{
	int First = 1;
	int *pValue = &First;
	auto Cleanup = co_await fg_AsyncDestroy
		(
			[&pValue]() -> TCFuture<int>
			{
				co_return *pValue;
			}
		)
	;
	int Later = 2;
	int *&pAlias = pValue;
	pAlias = &Later;
// CHECK-MESSAGES: :[[@LINE-1]]:2: warning: 'pValue', which the cleanup of 'fg_AsyncDestroy' uses, is pointed at 'Later', {{.*}}
	co_return 0;
}
TCFuture<int> fg_PointedThroughCopyAndExchange()
{
	int First = 1;
	int *pValue = &First;
	auto Cleanup = co_await fg_AsyncDestroy
		(
			[&pValue]() -> TCFuture<int>
			{
				co_return *pValue;
			}
		)
	;
	int Later = 2;
	int *pLater = &Later;
	pValue = pLater;
// CHECK-MESSAGES: :[[@LINE-1]]:2: warning: 'pValue', which the cleanup of 'fg_AsyncDestroy' uses, is pointed at 'Later', {{.*}}
	fg_Exchange(pValue, &Later);
// CHECK-MESSAGES: :[[@LINE-1]]:2: warning: 'pValue', which the cleanup of 'fg_AsyncDestroy' uses, is pointed at 'Later', {{.*}}
	co_return 0;
}
TCFuture<int> fg_MovedThroughPointer(TCActor<> _Target)
{
	auto Cleanup = co_await fg_AsyncDestroy(_Target);
	auto *pCleanup = &Cleanup;
	fg_StoreGuard(fg_Move(*pCleanup));
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: the result of 'fg_AsyncDestroy' is moved, so the cleanup can run after the locals it refers to are gone; keep it in its variable [malterlib-async-destroy]
	co_return 0;
}

// What isn't evaluated doesn't move the guard.
TCFuture<int> fg_Round53Unevaluated()
{
	int Value = 1;
	auto Guard = co_await fg_AsyncDestroy
		(
			[&]() -> TCFuture<int>
			{
				co_return Value;
			}
		)
	;
	(void)sizeof(fg_Move(Guard));
	co_return 0;
}

// A returned awaiter refers to the lambda it is given, an early join that a
// jump passes doesn't run, and neither does a use of the guard that isn't
// evaluated.
namespace NRound57
{
	// Like the real fg_AsyncDestroy, which refers to the function it is
	// given.
	template <typename t_FDestroy, typename t_FStored>
	struct TCAsyncDestroyAwaiter
	{
		t_FDestroy m_fDestroy;
	};
	template <typename tf_FDestroy>
	TCAsyncDestroyAwaiter<tf_FDestroy &, tf_FDestroy> fg_AsyncDestroy(tf_FDestroy &&_fDestroy)
	{
		return {_fDestroy};
	}
}
auto fg_Round57MakeCleanup()
{
	return NRound57::fg_AsyncDestroy
// CHECK-MESSAGES: :[[@LINE-1]]:9: warning: 'fg_AsyncDestroy' refers to its cleanup function until it is co_awaited, but the function it is returned from destroys that first; use fg_AsyncDestroyByValue [malterlib-async-destroy]
		(
			[Value = 42]() -> TCFuture<int>
			{
				co_return Value;
			}
		)
	;
}
TCFuture<int> fg_Round57Jumped()
{
	CPointedValue Early;
	CPointedValue *pValue = &Early;
	auto Cleanup = co_await fg_AsyncDestroy
		(
			[&pValue]() -> TCFuture<int>
			{
				co_return pValue->m_Value;
			}
		)
	;
	CPointedValue Late;
	goto Assign;
	co_await fg_Move(Cleanup);
Assign:
	pValue = &Late;
// CHECK-MESSAGES: :[[@LINE-1]]:2: warning: 'pValue', which the cleanup of 'fg_AsyncDestroy' uses, is pointed at 'Late', {{.*}}
	co_return 0;
}
TCFuture<int> fg_Round57Unevaluated()
{
	CPointedValue Early;
	CPointedValue *pValue = &Early;
	auto Cleanup = co_await fg_AsyncDestroy
		(
			[&pValue]() -> TCFuture<int>
			{
				co_return pValue->m_Value;
			}
		)
	;
	CPointedValue Late;
	static_cast<void>(noexcept(Cleanup.f_Clear()));
	pValue = &Late;
// CHECK-MESSAGES: :[[@LINE-1]]:2: warning: 'pValue', which the cleanup of 'fg_AsyncDestroy' uses, is pointed at 'Late', {{.*}}
	co_return 0;
}

// A pointer moved within a later array, and a returned awaiter of a cleanup
// given through a local reference to it.
void fg_Round62Consume(int _Value);
TCFuture<int> fg_Round62Offset()
{
	int *pValue = nullptr;
	auto Cleanup = co_await fg_AsyncDestroy
		(
			[&pValue]() -> TCFuture<int>
			{
				fg_Round62Consume(*pValue);
				co_return 0;
			}
		)
	;
	int Late[2] = {1, 2};
	pValue = Late + 1;
// CHECK-MESSAGES: :[[@LINE-1]]:2: warning: 'pValue', which the cleanup of 'fg_AsyncDestroy' uses, is pointed at 'Late', which is destroyed before the cleanup runs; point it at what is declared before the result of 'fg_AsyncDestroy' [malterlib-async-destroy]
	co_return 0;
}
auto fg_Round62Alias()
{
	auto fCleanup = [Value = 42]() -> TCFuture<int>
		{
			co_return Value;
		}
	;
	auto &fAlias = fCleanup;
	return NRound57::fg_AsyncDestroy(fAlias);
// CHECK-MESSAGES: :[[@LINE-1]]:9: warning: 'fg_AsyncDestroy' refers to its cleanup function until it is co_awaited, but the function it is returned from destroys that first; use fg_AsyncDestroyByValue [malterlib-async-destroy]
}

// A guard kept in an element of a local array lives as long as the array.
TCFuture<int> fg_Round63Array()
{
	CAsyncDestroy Guards[] =
		{
			co_await fg_AsyncDestroy
				(
					[]() -> TCFuture<int>
					{
						co_return 0;
					}
				)
		}
	;
	co_await fg_Suspend();
	co_return 0;
}

// An early await of the guard in parentheses is one.
TCFuture<int> fg_Round64Parenthesized()
{
	int Value = 0;
	auto Cleanup = co_await fg_AsyncDestroy
		(
			[&Value]() -> TCFuture<int>
			{
				co_return Value;
			}
		)
	;
	co_await (fg_Move(Cleanup));
	co_return 0;
}

// An awaiter that keeps a copy of the cleanup can be returned through a local
// it is kept in.
auto fg_Round65MakeCleanup()
{
	auto Awaiter = fg_AsyncDestroyByValue
		(
			[]() -> TCFuture<int>
			{
				co_return 0;
			}
		)
	;
	return Awaiter;
}

// A local cleanup given moved is still the local the returned awaiter refers
// to.
auto fg_Round70MakeCleanup()
{
	auto Cleanup = [Value = 42]() -> TCFuture<int>
		{
			co_return Value;
		}
	;
	return NRound57::fg_AsyncDestroy(fg_Move(Cleanup));
// CHECK-MESSAGES: :[[@LINE-1]]:9: warning: 'fg_AsyncDestroy' refers to its cleanup function until it is co_awaited, but the function it is returned from destroys that first; use fg_AsyncDestroyByValue [malterlib-async-destroy]
}

// A pointer pointed at a later local through a conditional, a cast or
// `std::addressof`.
namespace std
{
	template <typename t_CType>
	constexpr t_CType *addressof(t_CType &_Value) noexcept
	{
		return __builtin_addressof(_Value);
	}
}
struct CRound73Value
{
	int m_Value = 42;
	~CRound73Value();
};
TCFuture<int> fg_Round73Pointed(bool _bChoose)
{
	CRound73Value *pValue = nullptr;
	auto Guard = co_await fg_AsyncDestroy
		(
			[&pValue]() -> TCFuture<int>
			{
				co_return pValue ? pValue->m_Value : 0;
			}
		)
	;
	CRound73Value Late;
	pValue = _bChoose ? &Late : nullptr;
// CHECK-MESSAGES: :[[@LINE-1]]:2: warning: 'pValue', which the cleanup of 'fg_AsyncDestroy' uses, is pointed at 'Late', which is destroyed before the cleanup runs; point it at what is declared before the result of 'fg_AsyncDestroy' [malterlib-async-destroy]
	pValue = static_cast<CRound73Value *>(&Late);
// CHECK-MESSAGES: :[[@LINE-1]]:2: warning: 'pValue', which the cleanup of 'fg_AsyncDestroy' uses, is pointed at 'Late', which is destroyed before the cleanup runs; point it at what is declared before the result of 'fg_AsyncDestroy' [malterlib-async-destroy]
	pValue = std::addressof(Late);
// CHECK-MESSAGES: :[[@LINE-1]]:2: warning: 'pValue', which the cleanup of 'fg_AsyncDestroy' uses, is pointed at 'Late', which is destroyed before the cleanup runs; point it at what is declared before the result of 'fg_AsyncDestroy' [malterlib-async-destroy]
	co_return 0;
}

// A cleanup that a factory lambda makes, and a pointer it only resets.
void fg_Round77Use(int _Value);
TCFuture<int> fg_Round77Factory()
{
	int *pValue = nullptr;
	auto fMakeCleanup = [&pValue]
		{
			return [&pValue]() -> TCFuture<int>
				{
					fg_Round77Use(*pValue);
					co_return 0;
				}
			;
		}
	;
	auto Cleanup = co_await fg_AsyncDestroy(fMakeCleanup());
	int Late = 42;
	pValue = &Late;
// CHECK-MESSAGES: :[[@LINE-1]]:2: warning: 'pValue', which the cleanup of 'fg_AsyncDestroy' uses, is pointed at 'Late', which is destroyed before the cleanup runs; point it at what is declared before the result of 'fg_AsyncDestroy' [malterlib-async-destroy]
	co_return 0;
}

TCFuture<int> fg_Round77Reset()
{
	int *pValue = nullptr;
	auto Cleanup = co_await fg_AsyncDestroy
		(
			[&pValue]() -> TCFuture<int>
			{
				pValue = nullptr;
				co_return 0;
			}
		)
	;
	int Late = 42;
	pValue = &Late;
	co_return 0;
}

// A returned awaiter that refers to a local through a pointer to it.
auto fg_Round78ThroughPointer()
{
	auto fCleanup = [Value = 42]() -> TCFuture<int>
		{
			co_return Value;
		}
	;
	auto *pCleanup = &fCleanup;
	return NRound57::fg_AsyncDestroy(*pCleanup);
// CHECK-MESSAGES: :[[@LINE-1]]:9: warning: 'fg_AsyncDestroy' refers to its cleanup function until it is co_awaited, but the function it is returned from destroys that first; use fg_AsyncDestroyByValue [malterlib-async-destroy]
}

// Pointed at a later local through a cast to a reference.
TCFuture<int> fg_Round80Cast()
{
	int First = 1;
	int *pValue = &First;
	auto Cleanup = co_await fg_AsyncDestroy
		(
			[&pValue]() -> TCFuture<int>
			{
				co_return *pValue;
			}
		)
	;
	int Late = 2;
	static_cast<int *&>(pValue) = &Late;
// CHECK-MESSAGES: :[[@LINE-1]]:2: warning: 'pValue', which the cleanup of 'fg_AsyncDestroy' uses, is pointed at 'Late', which is destroyed before the cleanup runs; point it at what is declared before the result of 'fg_AsyncDestroy' [malterlib-async-destroy]
	co_return 0;
}

// A cleanup that a local reference extends goes with the function too.
auto fg_Round82MakeCleanup()
{
	auto &&fCleanup = [Value = 42]() -> TCFuture<int>
		{
			co_return Value;
		}
	;
	return NRound57::fg_AsyncDestroy(fCleanup);
// CHECK-MESSAGES: :[[@LINE-1]]:9: warning: 'fg_AsyncDestroy' refers to its cleanup function until it is co_awaited, but the function it is returned from destroys that first; use fg_AsyncDestroyByValue [malterlib-async-destroy]
}

// A pointer that the cleanup uses assigned as either of a conditional, and an
// await in a branch of an `if constexpr` that is discarded.
TCFuture<int> fg_Round84Either(bool _bFirst)
{
	int *pValue = nullptr;
	int *pOther = nullptr;
	auto Cleanup = co_await fg_AsyncDestroy
		(
			[&]() -> TCFuture<int>
			{
				co_return *pValue;
			}
		)
	;
	int Late = 42;
	(_bFirst ? pValue : pOther) = &Late;
// CHECK-MESSAGES: :[[@LINE-1]]:2: warning: 'pValue', which the cleanup of 'fg_AsyncDestroy' uses, is pointed at 'Late', which is destroyed before the cleanup runs; point it at what is declared before the result of 'fg_AsyncDestroy' [malterlib-async-destroy]
	co_return 0;
}
TCFuture<int> fg_Round84Discarded()
{
	if constexpr (false)
	{
		co_await fg_AsyncDestroy
			(
				[]() -> TCFuture<int>
				{
					co_return 0;
				}
			)
		;
	}
	co_return 0;
}

// Returned as the value of a future, `co_return`, it refers to the cleanup
// function too.
struct CRound85Cleanup
{
	int m_Value;
	TCFuture<int> operator()() const
	{
		co_return m_Value;
	}
};
using CRound85Awaiter = NRound57::TCAsyncDestroyAwaiter<CRound85Cleanup &, CRound85Cleanup &>;
TCFuture<CRound85Awaiter> fg_Round85Returned()
{
	CRound85Cleanup Cleanup{42};
	co_return NRound57::fg_AsyncDestroy(Cleanup);
// CHECK-MESSAGES: :[[@LINE-1]]:12: warning: 'fg_AsyncDestroy' refers to its cleanup function until it is co_awaited, but the function it is returned from destroys that first; use fg_AsyncDestroyByValue [malterlib-async-destroy]
}

// A pointer copied from one assigned a later local points to it.
struct CRound86Value
{
	int m_Value;
	~CRound86Value();
};
TCFuture<int> fg_Round86Copy()
{
	CRound86Value *pValue = nullptr;
	auto Guard = co_await fg_AsyncDestroy
		(
			[&pValue]() -> TCFuture<int>
			{
				co_return pValue->m_Value;
			}
		)
	;
	CRound86Value Late{42};
	CRound86Value *pLate = nullptr;
	pLate = &Late;
	pValue = pLate;
// CHECK-MESSAGES: :[[@LINE-1]]:2: warning: 'pValue', which the cleanup of 'fg_AsyncDestroy' uses, is pointed at 'Late', which is destroyed before the cleanup runs; point it at what is declared before the result of 'fg_AsyncDestroy' [malterlib-async-destroy]
	co_return 0;
}

// Awaiting the guard cast to an rvalue reference runs the cleanup there, like
// `fg_Move`.
TCFuture<int> fg_Round87Cast()
{
	auto Cleanup = co_await fg_AsyncDestroy
		(
			[]() -> TCFuture<int>
			{
				co_return 0;
			}
		)
	;
	co_await fg_Suspend();
	co_await static_cast<decltype(Cleanup) &&>(Cleanup);
	co_return 0;
}

// What isn't evaluated doesn't tear down.
struct CRound88Resource
{
	TCFuture<int> f_Destroy() noexcept;
};
TCFuture<int> fg_Round88Unevaluated(CRound88Resource _Resource)
{
	auto Exit = g_OnScopeExit / [&]
		{
			(void)sizeof(_Resource.f_Destroy());
		}
	;
	co_return 0;
}

// Through a reference to the pointer the cleanup captures.
TCFuture<int> fg_Round99Alias()
{
	int Early = 1;
	int *pValue = &Early;
	int *&pAlias = pValue;
	auto Cleanup = co_await fg_AsyncDestroy
		(
			[&pAlias]() -> TCFuture<int>
			{
				co_return *pAlias;
			}
		)
	;
	int Late = 2;
	pValue = &Late;
// CHECK-MESSAGES: :[[@LINE-1]]:2: warning: 'pValue', which the cleanup of 'fg_AsyncDestroy' uses, is pointed at 'Late', which is destroyed before the cleanup runs; point it at what is declared before the result of 'fg_AsyncDestroy' [malterlib-async-destroy]
	co_return 0;
}

// Moved out of an array of guards.
struct CRound100Resource
{
	TCFuture<int> f_Destroy() &&;
};
template <typename tf_CType>
void fg_Round100Store(tf_CType &&_Value);

TCFuture<int> fg_Round100Array()
{
	CRound100Resource Resource;
	using CGuard = decltype(fg_AsyncDestroy(Resource).await_resume());
	CGuard Guards[] = {co_await fg_AsyncDestroy(Resource)};
	fg_Round100Store(fg_Move(Guards[0]));
// CHECK-MESSAGES: :[[@LINE-1]]:19: warning: the result of 'fg_AsyncDestroy' is moved, so the cleanup can run after the locals it refers to are gone; keep it in its variable [malterlib-async-destroy]
	co_return 0;
}

// Teardown in a discarded branch of an `if constexpr`.
struct CRound101Resource
{
	TCFuture<int> f_Destroy();
};

TCFuture<int> fg_Round101Discarded()
{
	CRound101Resource Resource;
	auto Cleanup = g_OnScopeExit / [&]
		{
			if constexpr (false)
				Resource.f_Destroy();
		}
	;
	co_return 0;
}

// A guard initialized through a comma.
void fg_Round102Prepare();
TCFuture<int> fg_Round102Cleanup();

TCFuture<int> fg_Round102Comma()
{
	auto Guard =
		(
			fg_Round102Prepare()
			, co_await fg_AsyncDestroy
				(
					[]
					{
						return fg_Round102Cleanup();
					}
				)
		)
	;
	co_return 0;
}

// Moved out of a member of an aggregate that holds it.
struct CRound104Holder
{
	CAsyncDestroy m_Guard;
};
using FRound104Cleanup = NMib::NFunction::TCFunction<TCFuture<int>()>;
CAsyncDestroyAwaiter fg_Round104AsyncDestroy(FRound104Cleanup &&_fCleanup);

TCFuture<CAsyncDestroy> fg_Round104Escape(FRound104Cleanup _fCleanup)
{
	CRound104Holder Holder{co_await fg_Round104AsyncDestroy(fg_Move(_fCleanup))};
	co_return fg_Move(Holder.m_Guard);
}

// A label of a switch between the clear and the assignment enters after the
// clear.
TCFuture<int> fg_Round106SwitchEntry(int _Case)
{
	int *pValue = nullptr;
	auto Guard = co_await fg_AsyncDestroy
		(
			[&]() -> TCFuture<int>
			{
				co_return *pValue;
			}
		)
	;
	int Late = 1;
	switch (_Case)
	{
	case 0:
		Guard.f_Clear();
		[[fallthrough]];
	case 1:
		pValue = &Late;
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: 'pValue', which the cleanup of 'fg_AsyncDestroy' uses, is pointed at 'Late', which is destroyed before the cleanup runs; point it at what is declared before the result of 'fg_AsyncDestroy' [malterlib-async-destroy]
		break;
	}
	co_return 0;
}

// A move through a copy of a pointer to the guard.
template <typename t_CType>
void fg_Round106Keep(t_CType *_pKept);

TCFuture<int> fg_Round106PointerCopy()
{
	int Local = 7;
	auto Guard = co_await fg_AsyncDestroy
		(
			[&]() -> TCFuture<int>
			{
				co_return Local;
			}
		)
	;
	auto *pGuard = &Guard;
	auto *pCopy = pGuard;
	fg_Round106Keep(new decltype(Guard)(fg_Move(*pCopy)));
// CHECK-MESSAGES: :[[@LINE-1]]:38: warning: the result of 'fg_AsyncDestroy' is moved, so the cleanup can run after the locals it refers to are gone; keep it in its variable [malterlib-async-destroy]
	co_return 0;
}

// A pointer through as many copies as the code has, and copies that go
// around, which are looked into once.
struct CRound109Data
{
	int m_Value = 42;
	~CRound109Data();
};

TCFuture<int> fg_Round109Copies()
{
	int *pValue = nullptr, *pA = nullptr, *pB = nullptr, *pC = nullptr, *pD = nullptr, *pE = nullptr;
	auto Cleanup = co_await fg_AsyncDestroy
		(
			[&pValue]() -> TCFuture<int>
			{
				co_return *pValue;
			}
		)
	;
	CRound109Data Late;
	pA = &Late.m_Value;
	pB = pA;
	pC = pB;
	pD = pC;
	pE = pD;
	pValue = pE;
// CHECK-MESSAGES: :[[@LINE-1]]:2: warning: 'pValue', which the cleanup of 'fg_AsyncDestroy' uses, is pointed at 'Late', which is destroyed before the cleanup runs; point it at what is declared before the result of 'fg_AsyncDestroy' [malterlib-async-destroy]
	co_return 0;
}

#define DRound109Pair if (_bA) pValue = pOther; if (_bB) pOther = pValue;
#define DRound109Ten DRound109Pair DRound109Pair DRound109Pair DRound109Pair DRound109Pair DRound109Pair DRound109Pair DRound109Pair DRound109Pair DRound109Pair

TCFuture<int> fg_Round109Cycles(bool _bA, bool _bB)
{
	int X = 1, Y = 2;
	int *pValue = &X, *pOther = &Y;
	auto Cleanup = co_await fg_AsyncDestroy
		(
			[&pValue]() -> TCFuture<int>
			{
				co_return *pValue;
			}
		)
	;
	DRound109Ten DRound109Ten DRound109Ten
	co_return 0;
}

// Pointers made of conditionals of others many times are looked into once.
TCFuture<int> fg_Round110Conditionals(bool _bChoose)
{
	int *pValue = nullptr;
	auto Cleanup = co_await fg_AsyncDestroy
		(
			[&]() -> TCFuture<int>
			{
				co_return *pValue;
			}
		)
	;
	int Late = 0;
	int *p0 = &Late;
	int *p1 = _bChoose ? p0 : p0;
	int *p2 = _bChoose ? p1 : p1;
	int *p3 = _bChoose ? p2 : p2;
	int *p4 = _bChoose ? p3 : p3;
	int *p5 = _bChoose ? p4 : p4;
	int *p6 = _bChoose ? p5 : p5;
	int *p7 = _bChoose ? p6 : p6;
	int *p8 = _bChoose ? p7 : p7;
	int *p9 = _bChoose ? p8 : p8;
	int *p10 = _bChoose ? p9 : p9;
	int *p11 = _bChoose ? p10 : p10;
	int *p12 = _bChoose ? p11 : p11;
	int *p13 = _bChoose ? p12 : p12;
	int *p14 = _bChoose ? p13 : p13;
	int *p15 = _bChoose ? p14 : p14;
	int *p16 = _bChoose ? p15 : p15;
	int *p17 = _bChoose ? p16 : p16;
	int *p18 = _bChoose ? p17 : p17;
	int *p19 = _bChoose ? p18 : p18;
	int *p20 = _bChoose ? p19 : p19;
	int *p21 = _bChoose ? p20 : p20;
	int *p22 = _bChoose ? p21 : p21;
	int *p23 = _bChoose ? p22 : p22;
	int *p24 = _bChoose ? p23 : p23;
	pValue = p24;
// CHECK-MESSAGES: :[[@LINE-1]]:2: warning: 'pValue', which the cleanup of 'fg_AsyncDestroy' uses, is pointed at 'Late', which is destroyed before the cleanup runs; point it at what is declared before the result of 'fg_AsyncDestroy' [malterlib-async-destroy]
	co_return 0;
}

// A nested lambda called by name, and a reference to a local pointer.
TCFuture<int> fg_Round111Named()
{
	int Value = 1;
	auto Guard = co_await fg_AsyncDestroy
		(
			[&Value]() -> TCUnsafeFuture<int>
			{
				auto fRead = [&Value]
// CHECK-MESSAGES: :[[@LINE-1]]:20: warning: the cleanup function of 'fg_AsyncDestroy' uses 'Value', which it captures by reference, after its first suspension, where the reference is gone; copy or move it into a local before the suspension [malterlib-async-destroy]
					{
						return Value;
					}
				;
				co_await fg_Suspend();
				co_return fRead.operator()();
			}
		)
	;
	co_return 0;
}
TCFuture<int> fg_Round111PointerAlias()
{
	int Value = 1;
	auto Guard = co_await fg_AsyncDestroy
		(
			[&Value]() -> TCUnsafeFuture<int>
			{
				int *pValue = &Value;
				int *&pAlias = pValue;
				co_await fg_Suspend();
				co_return *pAlias;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: the cleanup function of 'fg_AsyncDestroy' uses 'pAlias', which it captures by reference, after its first suspension, where the reference is gone; copy or move it into a local before the suspension [malterlib-async-destroy]
			}
		)
	;
	co_return 0;
}

// What a getter gives of a local is in it; a reference is what it is bound to.
void fg_Round112Use(int);
struct CRound112Value
{
	int m_Value = 42;
	~CRound112Value()
	{
		m_Value = 0;
	}
	int *f_Get()
	{
		return &m_Value;
	}
};
TCFuture<int> fg_Round112Getter()
{
	int *p = nullptr;
	auto Guard = co_await fg_AsyncDestroy
		(
			[&]() -> TCFuture<int>
			{
				fg_Round112Use(*p);
				co_return 0;
			}
		)
	;
	CRound112Value Late;
	p = Late.f_Get();
// CHECK-MESSAGES: :[[@LINE-1]]:2: warning: 'p', which the cleanup of 'fg_AsyncDestroy' uses, is pointed at 'Late', which is destroyed before the cleanup runs; point it at what is declared before the result of 'fg_AsyncDestroy' [malterlib-async-destroy]
	co_return 0;
}
TCFuture<int> fg_Round112Alias()
{
	int Outer = 42;
	int *p = nullptr;
	auto Guard = co_await fg_AsyncDestroy
		(
			[&]() -> TCFuture<int>
			{
				fg_Round112Use(*p);
				co_return 0;
			}
		)
	;
	int &Alias = Outer;
	p = &Alias;
	co_return 0;
}

// Round 113: the object of a call whose arguments suspend is used after the suspension
namespace NRound113Early
{
TCFuture<int> work();
struct Data { int f_Read(int); };
TCFuture<int> f() {
  Data value;
  auto cleanup = co_await fg_AsyncDestroy([&value]() -> TCUnsafeFuture<int> {
    co_return value.f_Read(co_await work());
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: the cleanup function of 'fg_AsyncDestroy' uses 'value', which it captures by reference, after its first suspension, where the reference is gone; copy or move it into a local before the suspension [malterlib-async-destroy]
  });
  co_return 0;
}
}

// Round 113: a lambda captured by a lambda called after the suspension can be called
namespace NRound113Transitive
{
TCFuture<int> work();
TCFuture<int> f() {
  int value = 0;
  auto cleanup = co_await fg_AsyncDestroy([&value]() -> TCUnsafeFuture<int> {
    auto read = [&value] { return value; };
// CHECK-MESSAGES: :[[@LINE-1]]:19: warning: the cleanup function of 'fg_AsyncDestroy' uses 'value', which it captures by reference, after its first suspension, where the reference is gone; copy or move it into a local before the suspension [malterlib-async-destroy]
    auto indirect = [&read] { return read(); };
    co_await work();
    co_return indirect();
  });
  co_return 0;
}
}

// Round 114: g_OnScopeExit.operator/ called by name
namespace NRound114Named
{
struct Resource { TCFuture<int> f_Destroy(); };
TCFuture<int> fg_Run() {
  Resource resource;
  auto cleanup = g_OnScopeExit.operator/([&] { resource.f_Destroy(); });
// CHECK-MESSAGES: :[[@LINE-1]]:18: warning: the teardown of an actor or functor in a coroutine uses 'fg_AsyncDestroy' instead of 'g_OnScopeExit' [malterlib-async-destroy]
  co_return 0;
}
}

// Round 114: a setter that points the pointer at a later local
namespace NRound114Setter
{
struct Value { int n; ~Value(); };
void set(Value *&p, Value &v) { p = &v; }
TCFuture<int> test() {
  Value early{0};
  Value *p = &early;
  auto cleanup = co_await fg_AsyncDestroy([&p]() -> TCFuture<int> { co_return p->n; });
  Value late{42};
  set(p, late);
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: 'p', which the cleanup of 'fg_AsyncDestroy' uses, is pointed at 'late', which is destroyed before the cleanup runs; point it at what is declared before the result of 'fg_AsyncDestroy' [malterlib-async-destroy]
  co_return 0;
}
}

// Round 114: assignments that don't run
namespace NRound114NotRun
{
TCFuture<int> test() {
  int early = 0;
  int *p = &early;
  auto cleanup = co_await fg_AsyncDestroy([&p]() -> TCFuture<int> { co_return *p; });
  int late = 42;
  if constexpr (false)
    p = &late;
  auto unused = [&] { p = &late; };
  co_return 0;
}
}

// __builtin_addressof points at a late local like &.
namespace NRound116BuiltinAddress {
TCFuture<int> f() {
  int first = 1;
  int *p = &first;
  auto cleanup = co_await fg_AsyncDestroy(
    [&p]() -> TCFuture<int> { co_return *p; });
  int late = 2;
  p = __builtin_addressof(late);
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: 'p', which the cleanup of 'fg_AsyncDestroy' uses, is pointed at 'late', which is destroyed before the cleanup runs; point it at what is declared before the result of 'fg_AsyncDestroy' [malterlib-async-destroy]
  co_return 0;
}
}

// Round 120: the result joined by a cast to an rvalue reference, or on each
// path by either a join or a cancel, no longer runs the cleanup.
namespace NRound120Finished
{
TCFuture<int> fg_Cast() {
  int Early = 0;
  int *pValue = &Early;
  auto Cleanup = co_await fg_AsyncDestroy([&pValue]() -> TCFuture<int> { co_return *pValue; });
  co_await static_cast<decltype(Cleanup) &&>(Cleanup);
  int Late = 1;
  pValue = &Late;
  co_return 0;
}

TCFuture<int> fg_Branches(bool _bExecute) {
  int Early = 0;
  int *pValue = &Early;
  auto Cleanup = co_await fg_AsyncDestroy([&pValue]() -> TCFuture<int> { co_return *pValue; });
  if (_bExecute)
    co_await fg_Move(Cleanup);
  else
    Cleanup.f_Clear();
  int Late = 1;
  pValue = &Late;
  co_return 0;
}

TCFuture<int> fg_OneBranch(bool _bExecute) {
  int Early = 0;
  int *pValue = &Early;
  auto Cleanup = co_await fg_AsyncDestroy([&pValue]() -> TCFuture<int> { co_return *pValue; });
  if (_bExecute)
    co_await fg_Move(Cleanup);
  int Late = 1;
  pValue = &Late;
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: 'pValue', which the cleanup of 'fg_AsyncDestroy' uses, is pointed at 'Late', which is destroyed before the cleanup runs; point it at what is declared before the result of 'fg_AsyncDestroy' [malterlib-async-destroy]
  co_return 0;
}
}

// Round 121: a setter that points it where a reference to a pointer points.
namespace NRound121Setter
{
void fg_CopyPointer(int *&_pDestination, int *const &_pSource) { _pDestination = _pSource; }

TCFuture<int> fg_Run() {
  int *pValue = nullptr;
  auto Cleanup = co_await fg_AsyncDestroy([&pValue]() -> TCFuture<int> { co_return *pValue; });
  int Late = 7;
  int *pLate = &Late;
  fg_CopyPointer(pValue, pLate);
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: 'pValue', which the cleanup of 'fg_AsyncDestroy' uses, is pointed at 'Late', which is destroyed before the cleanup runs; point it at what is declared before the result of 'fg_AsyncDestroy' [malterlib-async-destroy]
  co_return 0;
}
}

// Round 123: a pointer given to a call that runs after its arguments suspend
// points to what the cleanup can have destroyed by then.
namespace NRound123Pointer
{
TCFuture<int> fg_Work();
struct CData {
  int m_Value = 42;
  ~CData() { m_Value = 0; }
};
int fg_Read(CData *_pData, int);
int fg_ReadCopy(CData _Data, int);

TCFuture<int> fg_Address() {
  CData Value;
  auto Guard = co_await fg_AsyncDestroy([&Value]() -> TCUnsafeFuture<int> {
    co_return fg_Read(&Value, co_await fg_Work());
// CHECK-MESSAGES: :[[@LINE-1]]:24: warning: the cleanup function of 'fg_AsyncDestroy' uses 'Value', which it captures by reference, after its first suspension, where the reference is gone; copy or move it into a local before the suspension [malterlib-async-destroy]
  });
  co_return 0;
}

TCFuture<int> fg_Pointer() {
  CData Value;
  auto Guard = co_await fg_AsyncDestroy([&Value]() -> TCUnsafeFuture<int> {
    CData *pValue = &Value;
    co_return fg_Read(pValue, co_await fg_Work());
// CHECK-MESSAGES: :[[@LINE-1]]:23: warning: the cleanup function of 'fg_AsyncDestroy' uses 'pValue', which it captures by reference, after its first suspension, where the reference is gone; copy or move it into a local before the suspension [malterlib-async-destroy]
  });
  co_return 0;
}

TCFuture<int> fg_Copy() {
  CData Value;
  auto Guard = co_await fg_AsyncDestroy([&Value]() -> TCUnsafeFuture<int> {
    co_return fg_ReadCopy(Value, co_await fg_Work());
  });
  co_return 0;
}
}

// Round 126: a cleanup kept in a local before the scope exit is made tears
// down too, and what isn't evaluated or a lambda that isn't called doesn't.
namespace NRound126
{
struct CResource {
  TCFuture<int> f_Destroy();
};

TCFuture<int> fg_Local(CResource _Resource) {
  auto fCleanup = [&] { _Resource.f_Destroy(); };
  auto Exit = g_OnScopeExit / fg_Move(fCleanup);
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: the teardown of an actor or functor in a coroutine uses 'fg_AsyncDestroy' instead of 'g_OnScopeExit' [malterlib-async-destroy]
  co_return 0;
}

TCFuture<int> fg_Decltype(CResource _Resource) {
  auto Exit = g_OnScopeExit / [&] {
    using CResult = decltype(_Resource.f_Destroy());
  };
  co_return 0;
}

TCFuture<int> fg_Uncalled(CResource _Resource) {
  auto Exit = g_OnScopeExit / [&] {
    auto fLater = [&] { _Resource.f_Destroy(); };
    (void)fLater;
  };
  co_return 0;
}
}

// Round 131: what a constructor is given by reference before another argument
// suspends it uses after.
namespace NRound131
{
TCFuture<int> fg_Work();

struct CRead {
  int m_Result;
  CRead(int const &_Value, int _Other) : m_Result(_Value + _Other) {}
};

TCFuture<int> fg_Construct() {
  int Value = 1;
  auto Cleanup = co_await fg_AsyncDestroy([&Value]() -> TCFuture<int> {
    co_return CRead{Value, co_await fg_Work()}.m_Result;
// CHECK-MESSAGES: :[[@LINE-1]]:21: warning: the cleanup function of 'fg_AsyncDestroy' uses 'Value', which it captures by reference, after its first suspension, where the reference is gone; copy or move it into a local before the suspension [malterlib-async-destroy]
  });
  co_return 0;
}
}

// Round 135: a lambda that fg_CallSafe runs assigns where it runs.
namespace NRound135
{
TCFuture<int> fg_Run() {
  int *pValue = nullptr;
  auto Cleanup = co_await fg_AsyncDestroy([&pValue]() -> TCFuture<int> {
    co_return *pValue;
  });
  int Late = 42;
  co_await fg_CallSafe([&]() -> TCFuture<int> {
    pValue = &Late;
// CHECK-MESSAGES: :[[@LINE-1]]:5: warning: 'pValue', which the cleanup of 'fg_AsyncDestroy' uses, is pointed at 'Late', which is destroyed before the cleanup runs; point it at what is declared before the result of 'fg_AsyncDestroy' [malterlib-async-destroy]
    co_return 0;
  });
  co_return 0;
}
}

namespace NRound137 {
struct CResource {
  TCFuture<int> f_Destroy();
};

// A branch that never runs, `if (false)`, doesn't tear down.
TCFuture<int> fg_Never(CResource _Resource) {
  auto Cleanup = g_OnScopeExit / [&] {
    if (false)
      _Resource.f_Destroy();
  };
  co_return 0;
}

// But one that can does.
TCFuture<int> fg_Maybe(CResource _Resource, bool _bDestroy) {
  auto Cleanup = g_OnScopeExit / [&] {
// CHECK-MESSAGES: :[[@LINE-1]]:18: warning: the teardown of an actor or functor in a coroutine uses 'fg_AsyncDestroy' instead of 'g_OnScopeExit' [malterlib-async-destroy]
    if (_bDestroy)
      _Resource.f_Destroy();
  };
  co_return 0;
}
}

namespace NRound139 {
struct CValue {
  int *m_pData = new int(42);
  ~CValue() { delete m_pData; }
};

// Through a chain of references, `CValue &Second = First;`.
TCFuture<int> fg_Chain() {
  CValue *pValue = nullptr;
  auto Cleanup = co_await fg_AsyncDestroy([&pValue]() -> TCFuture<int> {
    co_return *pValue->m_pData;
  });
  CValue Late;
  CValue &First = Late;
  CValue &Second = First;
  pValue = &Second;
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: 'pValue', which the cleanup of 'fg_AsyncDestroy' uses, is pointed at 'Late', which is destroyed before the cleanup runs; point it at what is declared before the result of 'fg_AsyncDestroy' [malterlib-async-destroy]
  co_return 0;
}
}

namespace NRound141 {
TCFuture<int> fg_Suspend();

struct CValue {
  int m_Value;
  int f_Read(int _Add) const { return m_Value + _Add; }
};

// The object of a call through what a pointer to it points to, `(&Value)[0]
// .f_Read(...)`.
TCFuture<int> fg_Indirect() {
  CValue Value{};
  auto Guard = co_await fg_AsyncDestroy([&Value]() -> TCFuture<int> {
    co_return (&Value)[0].f_Read(co_await fg_Suspend());
// CHECK-MESSAGES: :[[@LINE-1]]:17: warning: the cleanup function of 'fg_AsyncDestroy' uses 'Value', which it captures by reference, after its first suspension, where the reference is gone; copy or move it into a local before the suspension [malterlib-async-destroy]
  });
  co_return 0;
}
}

namespace std {
template <typename tf_FFunction>
auto invoke(tf_FFunction &&_fFunction) -> decltype(_fFunction()) {
  return _fFunction();
}
}

namespace NRound143 {
TCFuture<int> fg_Suspend();

// A lambda the cleanup runs by `std::invoke` after its suspension.
TCFuture<int> fg_Invoked() {
  int Value = 7;
  auto Guard = co_await fg_AsyncDestroy([&Value]() -> TCUnsafeFuture<int> {
    auto fRead = [&Value] { return Value; };
// CHECK-MESSAGES: :[[@LINE-1]]:20: warning: the cleanup function of 'fg_AsyncDestroy' uses 'Value', which it captures by reference, after its first suspension, where the reference is gone; copy or move it into a local before the suspension [malterlib-async-destroy]
    co_await fg_Suspend();
    co_return std::invoke(fRead);
  });
  co_return 0;
}

// A cleanup a factory makes.
TCFuture<int> fg_Made() {
  int Value = 7;
  auto fMake = [&] {
    return [&Value]() -> TCUnsafeFuture<int> {
      co_await fg_Suspend();
      co_return Value;
// CHECK-MESSAGES: :[[@LINE-1]]:17: warning: the cleanup function of 'fg_AsyncDestroy' uses 'Value', which it captures by reference, after its first suspension, where the reference is gone; copy or move it into a local before the suspension [malterlib-async-destroy]
    };
  };
  auto Guard = co_await fg_AsyncDestroy(fMake());
  co_return 0;
}
}

namespace NRound146 {
struct FCleanup
{
	TCFuture<void> operator()() const;
};
template <typename t_CType>
void fg_KeepUntilLater(t_CType *_pValue);

// Moved through a conditional, or through a reference bound to one.
TCFuture<int> fg_Conditional(bool _bChoose)
{
	auto Guard = co_await fg_AsyncDestroy(FCleanup{});
	fg_KeepUntilLater(new auto(fg_Move(_bChoose ? Guard : Guard)));
// CHECK-MESSAGES: :[[@LINE-1]]:29: warning: the result of 'fg_AsyncDestroy' is moved, so the cleanup can run after the locals it refers to are gone; keep it in its variable [malterlib-async-destroy]
	co_return 0;
}

TCFuture<int> fg_ConditionalAlias(bool _bChoose)
{
	auto Guard = co_await fg_AsyncDestroy(FCleanup{});
	auto &Alias = _bChoose ? Guard : Guard;
	fg_KeepUntilLater(new auto(fg_Move(Alias)));
// CHECK-MESSAGES: :[[@LINE-1]]:29: warning: the result of 'fg_AsyncDestroy' is moved, so the cleanup can run after the locals it refers to are gone; keep it in its variable [malterlib-async-destroy]
	co_return 0;
}
}

namespace NRound147 {
struct CResource
{
	TCFuture<int> f_Destroy();
};

// A requirement only tells whether the teardown is well-formed.
TCFuture<int> fg_Required(CResource &_Resource)
{
	auto Cleanup = g_OnScopeExit / [&_Resource]
		{
			static_assert(requires { _Resource.f_Destroy(); });
		}
	;
	co_return 0;
}

// A guard moved through a pointer pointed at it after it is made.
void fg_Save(CAsyncDestroy _Guard);

TCFuture<int> fg_ThroughAssignedPointer()
{
	int Value = 1;
	auto Guard = co_await fg_AsyncDestroy([&Value]() -> TCFuture<int> { co_return Value; });
	CAsyncDestroy *pGuard = nullptr;
	pGuard = &Guard;
	fg_Save(fg_Move(*pGuard));
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the result of 'fg_AsyncDestroy' is moved, so the cleanup can run after the locals it refers to are gone; keep it in its variable [malterlib-async-destroy]
	co_return 0;
}
}

namespace NRound148 {
// What a constant condition never runs.
TCFuture<int> fg_Never()
{
	if (false)
		fg_AsyncDestroy([]() -> TCFuture<int> { co_return 0; });
	co_return 0;
}
}

namespace NRound149 {
struct CResource
{
	TCFuture<int> f_Destroy();
};
using CGuard = decltype(fg_AsyncDestroy(*static_cast<CResource *>(nullptr)).await_resume());

// Through an unchanged local pointer to it.
TCFuture<CGuard> fg_Indirect()
{
	CResource Resource;
	auto fDestroy = &fg_AsyncDestroy<CResource &>;
	auto Guard = co_await fDestroy(Resource);
	co_return fg_Move(Guard);
// CHECK-MESSAGES: :[[@LINE-1]]:12: warning: the result of 'fg_AsyncDestroy' is moved, so the cleanup can run after the locals it refers to are gone; keep it in its variable [malterlib-async-destroy]
}

// A teardown in a local lambda that the cleanup calls.
TCFuture<int> fg_LocalLambda()
{
	CResource Resource;
	auto Cleanup = g_OnScopeExit / [&]
// CHECK-MESSAGES: :[[@LINE-1]]:17: warning: the teardown of an actor or functor in a coroutine uses 'fg_AsyncDestroy' instead of 'g_OnScopeExit' [malterlib-async-destroy]
		{
			auto fDestroy = [&] { Resource.f_Destroy(); };
			fDestroy();
		}
	;
	co_return 0;
}
}

namespace NRound150 {
int g_Global;

// A reference capture of what has static storage.
TCFuture<int> fg_Static()
{
	auto Cleanup = co_await fg_AsyncDestroy
		(
			[&Value = g_Global]() -> TCFuture<int>
			{
				co_await CAwaiter{};
				++Value;
				co_return 0;
			}
		)
	;
	co_return 0;
}
}

namespace NRound151 {
void fg_Prepare();

// Awaited after a comma.
TCFuture<int> fg_Comma()
{
	auto fCleanup = []() -> TCFuture<int> { co_return 0; };
	auto Guard = co_await (fg_Prepare(), fg_AsyncDestroy(fCleanup));
	co_return 0;
}

// Either of a conditional, awaited.
TCFuture<int> fg_Chosen(bool _bChoose)
{
	auto fCleanup = []() -> TCFuture<int> { co_return 0; };
	auto Guard = co_await (_bChoose ? fg_AsyncDestroy(fCleanup) : fg_AsyncDestroy(fCleanup));
	co_return 0;
}

// Returned with either of a conditional, which refers to the local.
namespace NModel
{
	template <typename t_FCleanup, typename t_FFinal>
	struct TCAsyncDestroyAwaiter
	{
		t_FCleanup m_fCleanup;
	};
	template <typename tf_FCleanup>
	TCAsyncDestroyAwaiter<tf_FCleanup &, tf_FCleanup> fg_AsyncDestroy(tf_FCleanup &_fCleanup)
	{
		return {_fCleanup};
	}
}

auto fg_Returned(bool _bChoose)
{
	auto fCleanup = []() -> TCFuture<int> { co_return 0; };
	return NModel::fg_AsyncDestroy(_bChoose ? fCleanup : fCleanup);
// CHECK-MESSAGES: :[[@LINE-1]]:9: warning: 'fg_AsyncDestroy' refers to its cleanup function until it is co_awaited, but the function it is returned from destroys that first; use fg_AsyncDestroyByValue [malterlib-async-destroy]
}
}

namespace NRound152 {
// Through a local pointer to it, dereferenced.
TCFuture<int> fg_Dereferenced()
{
	auto fCleanup = []() -> TCFuture<int> { co_return 0; };
	auto fDestroy = &fg_AsyncDestroy<decltype(fCleanup) &>;
	(void)co_await (*fDestroy)(fCleanup);
// CHECK-MESSAGES: :[[@LINE-1]]:8: warning: the result of co_awaiting 'fg_AsyncDestroy' is not kept in a named variable, so the cleanup runs at the end of the statement instead of at the end of the scope [malterlib-async-destroy]
	co_return 0;
}
}

namespace NRound160 {
// Pointed at a later local through a copy of a pointer to the pointer.
struct CValue {
  int m_Value;
  ~CValue();
};
TCFuture<int> fg_Run() {
  int *pValue = nullptr;
  auto Cleanup = co_await fg_AsyncDestroy([&pValue]() -> TCFuture<int> { co_return *pValue; });
  CValue Late{42};
  int **ppValue = &pValue;
  int **ppCopy = ppValue;
  *ppCopy = &Late.m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: 'pValue', which the cleanup of 'fg_AsyncDestroy' uses, is pointed at 'Late', which is destroyed before the cleanup runs; point it at what is declared before the result of 'fg_AsyncDestroy' [malterlib-async-destroy]
  co_return 0;
}
}

namespace NRound162 {
// Given through a cast to a reference to it.
template <typename t_FDestroy, typename t_FStored>
struct TCAsyncDestroyAwaiter {
  t_FDestroy m_fDestroy;
};
template <typename tf_FDestroy>
TCAsyncDestroyAwaiter<tf_FDestroy &, tf_FDestroy> fg_AsyncDestroy(tf_FDestroy &&_fDestroy) {
  return {_fDestroy};
}
auto fg_MakeCleanup() {
  auto fCleanup = [Value = 42]() -> TCFuture<int> { co_return Value; };
  return NRound162::fg_AsyncDestroy(static_cast<decltype(fCleanup) &&>(fCleanup));
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: 'fg_AsyncDestroy' refers to its cleanup function until it is co_awaited, but the function it is returned from destroys that first; use fg_AsyncDestroyByValue [malterlib-async-destroy]
}
}

namespace NRound163 {
int g_Global;
// Bound through a local reference to what has static storage.
TCFuture<int> fg_Run() {
  int &Alias = g_Global;
  auto Cleanup = co_await fg_AsyncDestroy([&Value = Alias]() -> TCFuture<int> {
    co_await CAwaiter{};
    co_return Value;
  });
  co_return 0;
}
}

namespace NRound165 {
// Not an assignment that a constant condition never runs.
TCFuture<int> fg_NeverTaken() {
  int Early = 1;
  int *pValue = &Early;
  auto Cleanup = co_await fg_AsyncDestroy(
      [&pValue]() -> TCFuture<int> { co_return *pValue; });
  int Late = 2;
  if (false)
    pValue = &Late;
  co_return 0;
}
}

namespace NRound167 {
void fg_SaveForLater(CAsyncDestroy _Guard);
// Moved through a cast to a reference to it, and as one of many
// alternatives.
TCFuture<int> fg_Cast(TCActor<> _Actor) {
  auto Guard = co_await fg_AsyncDestroy(_Actor);
  auto &Alias = static_cast<decltype(Guard) &>(Guard);
  fg_SaveForLater(fg_Move(Alias));
// CHECK-MESSAGES: :[[@LINE-1]]:19: warning: the result of 'fg_AsyncDestroy' is moved, so the cleanup can run after the locals it refers to are gone; keep it in its variable [malterlib-async-destroy]
  co_return 0;
}
#define DTwice(d_Value) (_bChoose ? (d_Value) : (d_Value))
#define DSixtyFour(d_Value)                                                    \
  DTwice(DTwice(DTwice(DTwice(DTwice(DTwice(d_Value))))))
TCFuture<CAsyncDestroy> fg_Many(bool _bChoose, CAsyncDestroy &_Other) {
  int Value = 1;
  auto Guard = co_await fg_AsyncDestroy(
      [&]() -> TCFuture<int> { co_return Value; });
  co_return fg_Move(_bChoose ? Guard : DSixtyFour(_Other));
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: the result of 'fg_AsyncDestroy' is moved, so the cleanup can run after the locals it refers to are gone; keep it in its variable [malterlib-async-destroy]
}
#undef DSixtyFour
#undef DTwice
// Through a copy of a local pointer to it.
struct CResource {
  TCFuture<int> f_Destroy();
};
TCFuture<CAsyncDestroy> fg_Copied() {
  CResource Resource;
  auto fDestroy = &fg_AsyncDestroy<CResource &>;
  auto fCopied = fDestroy;
  auto Guard = co_await fCopied(Resource);
  co_return fg_Move(Guard);
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: the result of 'fg_AsyncDestroy' is moved, so the cleanup can run after the locals it refers to are gone; keep it in its variable [malterlib-async-destroy]
}
}

namespace NRound168 {
// Pointed at a later local through a cast of a pointer to it.
TCFuture<int> fg_Cast() {
  int *pValue = nullptr;
  auto Guard = co_await fg_AsyncDestroy(
      [&pValue]() -> TCFuture<int> { co_return *pValue; });
  int Late = 1;
  *static_cast<int **>(&pValue) = &Late;
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: 'pValue', which the cleanup of 'fg_AsyncDestroy' uses, is pointed at 'Late', which is destroyed before the cleanup runs; point it at what is declared before the result of 'fg_AsyncDestroy' [malterlib-async-destroy]
  co_return 0;
}
}

namespace NRound170 {
// Torn down through a pointer to the member function.
struct CResource {
  TCFuture<int> f_Destroy();
};
TCFuture<int> fg_Run() {
  CResource Resource;
  auto fDestroy = &CResource::f_Destroy;
  auto Cleanup = g_OnScopeExit / [&] { (Resource.*fDestroy)(); };
// CHECK-MESSAGES: :[[@LINE-1]]:18: warning: the teardown of an actor or functor in a coroutine uses 'fg_AsyncDestroy' instead of 'g_OnScopeExit' [malterlib-async-destroy]
  co_return 0;
}
}

namespace NRound171 {
namespace NModel {
template <typename t_FCleanup> struct TCAsyncDestroyAwaiter {
  t_FCleanup m_fCleanup;
};
template <typename tf_FCleanup>
auto fg_AsyncDestroy(tf_FCleanup &_fCleanup) {
  return TCAsyncDestroyAwaiter<tf_FCleanup &>{_fCleanup};
}
} // namespace NModel
void fg_Prepare();
// A local cleanup given after a comma, or through `*&`.
auto fg_Comma() {
  auto fCleanup = []() -> TCFuture<int> { co_return 0; };
  return NModel::fg_AsyncDestroy((fg_Prepare(), fCleanup));
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: 'fg_AsyncDestroy' refers to its cleanup function until it is co_awaited, but the function it is returned from destroys that first; use fg_AsyncDestroyByValue [malterlib-async-destroy]
}
auto fg_Address() {
  auto fCleanup = []() -> TCFuture<int> { co_return 0; };
  return NModel::fg_AsyncDestroy(*&fCleanup);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: 'fg_AsyncDestroy' refers to its cleanup function until it is co_awaited, but the function it is returned from destroys that first; use fg_AsyncDestroyByValue [malterlib-async-destroy]
}
}

namespace NRound173 {
// Of a conditional, only what a constant condition chooses.
struct CCleanup {
  TCFuture<int> operator()() const;
};
template <class t_FCleanup, class t_CFinal> struct TCAsyncDestroyAwaiter {
  t_FCleanup m_fCleanup;
};
TCAsyncDestroyAwaiter<CCleanup &, CCleanup> fg_AsyncDestroy(CCleanup &_Cleanup);
CCleanup g_Cleanup;
auto fg_Global() {
  CCleanup Local;
  return fg_AsyncDestroy(true ? g_Cleanup : Local);
}
auto fg_LocalChosen() {
  CCleanup Local;
  return fg_AsyncDestroy(false ? g_Cleanup : Local);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: 'fg_AsyncDestroy' refers to its cleanup function until it is co_awaited, but the function it is returned from destroys that first; use fg_AsyncDestroyByValue [malterlib-async-destroy]
}
}

namespace NRound173Forwarded {
// Forwarded as an rvalue and awaited, which runs it early.
TCFuture<int> fg_Forwarded() {
  int Value = 1;
  auto Guard = co_await fg_AsyncDestroy(
      [&Value]() -> TCFuture<int> { co_return Value; });
  co_await fg_Forward<decltype(Guard)>(Guard);
  co_return 0;
}
}

namespace NRound176 {
TCFuture<int> fg_Work();
void fg_Read(int const *_pValue, int _Other);
// Given cast to a call that suspends in its other arguments.
TCFuture<int> fg_Cast() {
  int Value = 1;
  auto Guard = co_await fg_AsyncDestroy([&Value]() -> TCFuture<int> {
    fg_Read(static_cast<int const *>(&Value), co_await fg_Work());
// CHECK-MESSAGES: :[[@LINE-1]]:39: warning: the cleanup function of 'fg_AsyncDestroy' uses 'Value', which it captures by reference, after its first suspension, where the reference is gone; copy or move it into a local before the suspension [malterlib-async-destroy]
    co_return 0;
  });
  co_return 0;
}
// A pointer made with addressof.
TCFuture<int> fg_AddressOf() {
  int Value = 1;
  auto Guard = co_await fg_AsyncDestroy([&Value]() -> TCFuture<int> {
    int *pValue = __builtin_addressof(Value);
    co_await fg_Work();
    co_return *pValue;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: the cleanup function of 'fg_AsyncDestroy' uses 'pValue', which it captures by reference, after its first suspension, where the reference is gone; copy or move it into a local before the suspension [malterlib-async-destroy]
  });
  co_return 0;
}
}

namespace NRound177 {
template <typename t_CType>
void fg_Swap(t_CType &_Left, t_CType &_Right) {
  t_CType Temporary = fg_Move(_Left);
  _Left = fg_Move(_Right);
  _Right = fg_Move(Temporary);
}
TCFuture<int> fg_Wait();
// Swapped with a pointer at a local destroyed before the cleanup runs.
TCFuture<int> fg_Swapped() {
  int *pValue = nullptr;
  auto Cleanup = co_await fg_AsyncDestroy(
      [&pValue]() -> TCFuture<int> { co_return *pValue; });
  int Late = 42;
  int *pLate = &Late;
  fg_Swap(pValue, pLate);
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: 'pValue', which the cleanup of 'fg_AsyncDestroy' uses, is pointed at 'Late', which is destroyed before the cleanup runs; point it at what is declared before the result of 'fg_AsyncDestroy' [malterlib-async-destroy]
  co_return 0;
}
// A reference to a global, captured by reference.
int g_Value = 42;
TCFuture<int> fg_Global() {
  int &Value = g_Value;
  auto Cleanup = co_await fg_AsyncDestroy([&Value]() -> TCFuture<int> {
    co_await fg_Wait();
    co_return Value;
  });
  co_return 0;
}
// Pointed at either of two locals through a reference.
struct CValue {
  int f_Read() const;
  ~CValue();
};
TCFuture<int> fg_Chosen(bool _bChoose) {
  CValue *pValue = nullptr;
  auto Cleanup = co_await fg_AsyncDestroy([&pValue]() -> TCFuture<int> {
    co_return pValue->f_Read();
  });
  CValue First, Second;
  CValue &Alias = _bChoose ? First : Second;
  pValue = &Alias;
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: 'pValue', which the cleanup of 'fg_AsyncDestroy' uses, is pointed at 'Second', which is destroyed before the cleanup runs; point it at what is declared before the result of 'fg_AsyncDestroy' [malterlib-async-destroy]
  co_return 0;
}
}

namespace NRound178 {
template <typename t_FCleanup, typename t_FFinal>
struct TCAsyncDestroyAwaiter {
  t_FCleanup m_fCleanup;
};
template <typename t_FCleanup>
auto fg_AsyncDestroy(t_FCleanup &_fCleanup) {
  return TCAsyncDestroyAwaiter<t_FCleanup &, t_FCleanup>{_fCleanup};
}
// A cleanup given by reference through many conditional aliases, of a local
// that is gone when the function returns.
auto fg_Factory(bool _bChoose) {
  auto fCleanup = []() -> TCFuture<int> { co_return 0; };
  auto &r0 = fCleanup;
#define DRound178Link(d_N, d_P) auto &r##d_N = _bChoose ? r##d_P : r##d_P;
  DRound178Link(1, 0) DRound178Link(2, 1) DRound178Link(3, 2)
  DRound178Link(4, 3) DRound178Link(5, 4) DRound178Link(6, 5)
  DRound178Link(7, 6) DRound178Link(8, 7) DRound178Link(9, 8)
  DRound178Link(10, 9) DRound178Link(11, 10) DRound178Link(12, 11)
  DRound178Link(13, 12) DRound178Link(14, 13) DRound178Link(15, 14)
  DRound178Link(16, 15) DRound178Link(17, 16) DRound178Link(18, 17)
  DRound178Link(19, 18) DRound178Link(20, 19)
#undef DRound178Link
  return fg_AsyncDestroy(r20);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: 'fg_AsyncDestroy' refers to its cleanup function until it is co_awaited, but the function it is returned from destroys that first; use fg_AsyncDestroyByValue [malterlib-async-destroy]
}
}

namespace NRound180 {
struct CData {
  int m_Value = 42;
  ~CData();
};
// Pointed at a local destroyed before the cleanup runs, through a chain of
// references.
TCFuture<int> fg_Aliased() {
  int *pValue = nullptr;
  auto Guard = co_await fg_AsyncDestroy([&pValue]() -> TCFuture<int> {
    co_return *pValue;
  });
  CData Later;
  int *&rA = pValue;
  int *&rB = rA;
  int *&rC = rB;
  int *&rD = rC;
  int *&rE = rD;
  rE = &Later.m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: 'pValue', which the cleanup of 'fg_AsyncDestroy' uses, is pointed at 'Later', which is destroyed before the cleanup runs; point it at what is declared before the result of 'fg_AsyncDestroy' [malterlib-async-destroy]
  co_return 0;
}
// A guard that initializes a member of an aggregate made with parentheses.
struct CGuardHolder {
  CAsyncDestroy m_Guard;
};
TCFuture<int> fg_Held() {
  CGuardHolder Holder(co_await fg_AsyncDestroy([]() -> TCFuture<int> { co_return 0; }));
  co_return 0;
}
// Destroyed asynchronously in a scope exit through std::invoke.
struct CResource {
  TCFuture<int> f_Destroy();
};
TCFuture<int> fg_Invoked() {
  CResource Resource;
  auto Cleanup = g_OnScopeExit / [&] { std::invoke([&] { Resource.f_Destroy(); }); };
// CHECK-MESSAGES: :[[@LINE-1]]:18: warning: the teardown of an actor or functor in a coroutine uses 'fg_AsyncDestroy' instead of 'g_OnScopeExit' [malterlib-async-destroy]
  co_return 0;
}
}

namespace NRound192 {
struct CName {
  unsigned f_GetLen() const;
};
void fg_Read(unsigned);
// A reference bound to either of a conditional, also through a reference.
TCFuture<int> fg_Run(bool _bChoose) {
  CName Early;
  CName const *pName = nullptr;
  auto Cleanup = co_await fg_AsyncDestroy([&pName]() -> TCFuture<int> {
    if (pName)
      fg_Read(pName->f_GetLen());
    co_return 0;
  });
  CName Late;
  CName const &Alias = Late;
  CName const &Name = _bChoose ? Alias : Early;
  pName = &Name;
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: 'pName', which the cleanup of 'fg_AsyncDestroy' uses, is pointed at 'Late', which is destroyed before the cleanup runs; point it at what is declared before the result of 'fg_AsyncDestroy' [malterlib-async-destroy]
  co_return 0;
}
}

namespace NRound195 {
TCFuture<int> fg_Work();
// What only names a static member of what is captured doesn't use it.
struct CDigest {
  static constexpr int mc_Size = 32;
  int m_Value = 0;
};
TCFuture<int> fg_Test() {
  CDigest Digest;
  auto Cleanup = co_await fg_AsyncDestroy([&Digest]() -> TCFuture<int> {
    co_await fg_Work();
    co_return Digest.mc_Size + Digest.m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:32: warning: the cleanup function of 'fg_AsyncDestroy' uses 'Digest', which it captures by reference, after its first suspension, where the reference is gone; copy or move it into a local before the suspension [malterlib-async-destroy]
  });
  co_return 0;
}
}

namespace NRound196 {
int g_Value = 42;
// A reference to either of two pointers.
TCFuture<int> fg_Either(bool _bChoose) {
  int *p = nullptr;
  int *q = nullptr;
  int *&Alias = _bChoose ? p : q;
  auto Cleanup = co_await fg_AsyncDestroy([&Alias]() -> TCFuture<int> {
    co_return Alias ? *Alias : 0;
  });
  int Late = 1;
  p = &Late;
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: 'p', which the cleanup of 'fg_AsyncDestroy' uses, is pointed at 'Late', which is destroyed before the cleanup runs; point it at what is declared before the result of 'fg_AsyncDestroy' [malterlib-async-destroy]
  q = &Late;
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: 'q', which the cleanup of 'fg_AsyncDestroy' uses, is pointed at 'Late', which is destroyed before the cleanup runs; point it at what is declared before the result of 'fg_AsyncDestroy' [malterlib-async-destroy]
  co_return 0;
}
// A cleanup that points it elsewhere before it reads it, or reads it only
// where it isn't evaluated, doesn't use what it is pointed at here.
TCFuture<int> fg_Replaced() {
  int *p = nullptr;
  auto Cleanup = co_await fg_AsyncDestroy([&p]() -> TCFuture<int> {
    p = &g_Value;
    co_return *p;
  });
  int Late = 1;
  p = &Late;
  co_return 0;
}
TCFuture<int> fg_Unevaluated() {
  int *p = nullptr;
  auto Cleanup = co_await fg_AsyncDestroy([&p]() -> TCFuture<int> {
    co_return sizeof(*p);
  });
  int Late = 1;
  p = &Late;
  co_return 0;
}
TCFuture<int> fg_ReadFirst() {
  int *p = nullptr;
  auto Cleanup = co_await fg_AsyncDestroy([&p]() -> TCFuture<int> {
    int Old = p ? *p : 0;
    p = &g_Value;
    co_return Old;
  });
  int Late = 1;
  p = &Late;
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: 'p', which the cleanup of 'fg_AsyncDestroy' uses, is pointed at 'Late', which is destroyed before the cleanup runs; point it at what is declared before the result of 'fg_AsyncDestroy' [malterlib-async-destroy]
  co_return 0;
}
// A chain of references each to either of the one before is followed once
// each.
#define LINK(N, P) int &r##N = _bChoose ? r##P : r##P
TCFuture<int> fg_Chain(bool _bChoose) {
  int Value = 1;
  int *p = nullptr;
  auto Cleanup = co_await fg_AsyncDestroy([&p]() -> TCFuture<int> { co_return *p; });
  int &r0 = Value;
  LINK(1, 0); LINK(2, 1); LINK(3, 2); LINK(4, 3); LINK(5, 4); LINK(6, 5); LINK(7, 6); LINK(8, 7);
  LINK(9, 8); LINK(10, 9); LINK(11, 10); LINK(12, 11); LINK(13, 12); LINK(14, 13); LINK(15, 14);
  LINK(16, 15); LINK(17, 16); LINK(18, 17); LINK(19, 18); LINK(20, 19); LINK(21, 20); LINK(22, 21);
  LINK(23, 22); LINK(24, 23); LINK(25, 24); LINK(26, 25); LINK(27, 26); LINK(28, 27); LINK(29, 28);
  LINK(30, 29); LINK(31, 30); LINK(32, 31); LINK(33, 32); LINK(34, 33); LINK(35, 34); LINK(36, 35);
  LINK(37, 36); LINK(38, 37); LINK(39, 38); LINK(40, 39);
  p = &r40;
  co_return 0;
}
#undef LINK
}

// Not in C++23, where clang itself makes a constant of the whole object, which
// grows exponentially with how deep the types nest too.
#if __cplusplus < 202302L
namespace NRound249 {
// A member of a type that many members have is looked at once, in time linear
// in the types, not exponential in how deep they nest.
template <int t_Depth> struct TCResult {
  TCResult();
  TCResult(TCResult &&);
  TCResult<t_Depth - 1> m_A, m_B, m_C, m_D, m_E, m_F, m_G, m_H, m_I, m_J, m_K,
      m_L, m_M, m_N, m_O, m_P;
};
template <> struct TCResult<0> {
  int m_Value;
};
struct CHolder {
  CAsyncDestroy m_Guard;
  TCResult<12> m_Result;
};
TCFuture<int> fg_Run() {
  CHolder Holder{
      co_await fg_AsyncDestroy([]() -> TCFuture<int> { co_return 0; }), {}};
  TCResult<12> Result = fg_Move(Holder.m_Result);
  co_return 0;
}
} // namespace NRound249
#endif

namespace NRound202 {
// Moving another member of an aggregate that holds it leaves it in place;
// moving the one that holds it, also through another aggregate, doesn't.
struct CResult {
  int m_Value = 0;
  CResult() = default;
  CResult(CResult &&);
};
struct CInner {
  CAsyncDestroy m_Guard;
};
template <class t_CGuard>
struct TCOperation {
  t_CGuard m_Cleanup;
  CResult m_Result;
};
TCFuture<int> fg_Result() {
  TCOperation<CAsyncDestroy> Operation{
      co_await fg_AsyncDestroy([]() -> TCFuture<int> { co_return 0; }),
      CResult{}};
  CResult Result = fg_Move(Operation.m_Result);
  co_return Result.m_Value;
}
TCFuture<int> fg_Cleanup() {
  TCOperation<CAsyncDestroy> Operation{
      co_await fg_AsyncDestroy([]() -> TCFuture<int> { co_return 0; }),
      CResult{}};
  CAsyncDestroy Cleanup = fg_Move(Operation.m_Cleanup);
// CHECK-MESSAGES: :[[@LINE-1]]:27: warning: the result of 'fg_AsyncDestroy' is moved, so the cleanup can run after the locals it refers to are gone; keep it in its variable [malterlib-async-destroy]
  co_return 0;
}
TCFuture<int> fg_Inner() {
  TCOperation<CInner> Operation{
      {co_await fg_AsyncDestroy([]() -> TCFuture<int> { co_return 0; })},
      CResult{}};
  CInner Inner = fg_Move(Operation.m_Cleanup);
// CHECK-MESSAGES: :[[@LINE-1]]:18: warning: the result of 'fg_AsyncDestroy' is moved, so the cleanup can run after the locals it refers to are gone; keep it in its variable [malterlib-async-destroy]
  co_return 0;
}
}

namespace NRound203 {
// What a factory gives of the branch of an `if constexpr` it keeps.
struct CValue {
  int m_Value;
  ~CValue();
};
TCFuture<int> fg_Run() {
  CValue *pValue = nullptr;
  auto fMakeCleanup = [&pValue] {
    if constexpr (false)
      return []() -> TCFuture<int> { co_return 0; };
    else
      return [&pValue]() -> TCFuture<int> { co_return pValue->m_Value; };
  };
  auto Cleanup = co_await fg_AsyncDestroy(fMakeCleanup());
  CValue Late{42};
  pValue = &Late;
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: 'pValue', which the cleanup of 'fg_AsyncDestroy' uses, is pointed at 'Late', which is destroyed before the cleanup runs; point it at what is declared before the result of 'fg_AsyncDestroy' [malterlib-async-destroy]
  co_return 0;
}
}

namespace NRound205 {
// Forwarded as an lvalue, which moves nothing.
TCFuture<int> fg_Run() {
  int Value = 1;
  auto Cleanup = co_await fg_AsyncDestroy([&Value]() -> TCFuture<int> {
    (void)Value;
    co_return 0;
  });
  auto &Alias = fg_Forward<decltype(Cleanup) &>(Cleanup);
  co_return 0;
}
}

namespace NRound206 {
// Bound to a reference by what moves it, which moves nothing.
TCFuture<int> fg_Test() {
  auto Cleanup =
      co_await fg_AsyncDestroy([]() -> TCFuture<int> { co_return 0; });
  auto &&Alias = fg_Move(Cleanup);
  co_await fg_Move(Alias);
  co_return 0;
}
}

namespace NRound208 {
TCFuture<int> fg_Suspend();

struct CValue {
  int m_Value;
};
int fg_Finish(CValue const &_Value, int _Result);

// What a conditional chooses between, given by reference before the
// suspension.
TCFuture<int> fg_Conditional(bool _bFirst) {
  CValue First{}, Second{};
  auto Guard = co_await fg_AsyncDestroy([&, _bFirst]() -> TCFuture<int> {
    co_return fg_Finish(_bFirst ? First : Second, co_await fg_Suspend());
// CHECK-MESSAGES: :[[@LINE-1]]:35: warning: the cleanup function of 'fg_AsyncDestroy' uses 'First', which it captures by reference, after its first suspension, where the reference is gone; copy or move it into a local before the suspension [malterlib-async-destroy]
// CHECK-MESSAGES: :[[@LINE-2]]:43: warning: the cleanup function of 'fg_AsyncDestroy' uses 'Second', which it captures by reference, after its first suspension, where the reference is gone; copy or move it into a local before the suspension [malterlib-async-destroy]
  });
  co_return 0;
}

// Only the one a constant condition chooses, here a copy the cleanup owns.
TCFuture<int> fg_ConstantConditional() {
  CValue First{};
  auto Guard = co_await fg_AsyncDestroy([&First]() -> TCFuture<int> {
    CValue Own{};
    co_return fg_Finish(false ? First : Own, co_await fg_Suspend());
  });
  co_return 0;
}
}

namespace NRound210 {
struct CValue {
  int m_Value;
  ~CValue() {}
};
// Pointed at a local declared after the result by a lambda through what it is
// given, or at a member of one by a function.
void fg_SetMember(int *&o_pValue, CValue &_Value) { o_pValue = &_Value.m_Value; }
TCFuture<int> fg_Lambda() {
  int *pValue = nullptr;
  auto Cleanup = co_await fg_AsyncDestroy([&pValue]() -> TCFuture<int> {
    int Copy = *pValue;
    co_return Copy;
  });
  int Value = 1;
  auto fSet = [](int *&o_pValue, int *_pValue) { o_pValue = _pValue; };
  fSet(pValue, &Value);
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: 'pValue', which the cleanup of 'fg_AsyncDestroy' uses, is pointed at 'Value', which is destroyed before the cleanup runs; point it at what is declared before the result of 'fg_AsyncDestroy' [malterlib-async-destroy]
  co_return 0;
}
TCFuture<int> fg_Member() {
  int *pValue = nullptr;
  auto Cleanup = co_await fg_AsyncDestroy([&pValue]() -> TCFuture<int> {
    int Copy = *pValue;
    co_return Copy;
  });
  CValue Late{42};
  fg_SetMember(pValue, Late);
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: 'pValue', which the cleanup of 'fg_AsyncDestroy' uses, is pointed at 'Late', which is destroyed before the cleanup runs; point it at what is declared before the result of 'fg_AsyncDestroy' [malterlib-async-destroy]
  co_return 0;
}
}

namespace NRound212 {
struct CResource : CActor {
  TCFuture<int> f_Destroy();
};
extern int g_nFinished;
// Teardown through a helper whose body can be seen.
TCFuture<int> fg_DestroySubscription(CResource &_Resource) {
  return _Resource.f_Destroy();
}
TCFuture<int> fg_Helper(CResource Resource) {
  auto Cleanup = g_OnScopeExit / [&] {
// CHECK-MESSAGES: :[[@LINE-1]]:18: warning: the teardown of an actor or functor in a coroutine uses 'fg_AsyncDestroy' instead of 'g_OnScopeExit' [malterlib-async-destroy]
    (void)fg_DestroySubscription(Resource);
  };
  co_return 0;
}
// Not after a branch that always returns.
TCFuture<int> fg_EarlyReturn(CResource Resource) {
  constexpr bool c_bNeedsDestroy = false;
  auto Cleanup = g_OnScopeExit / [&] {
    ++g_nFinished;
    if constexpr (!c_bNeedsDestroy)
      return;
    (void)Resource.f_Destroy();
  };
  co_return 0;
}
}

namespace NRound213 {
template <class t_FCleanup> struct TCOnScopeExit {
  t_FCleanup mp_fOnExit;
  explicit TCOnScopeExit(t_FCleanup &&_fOnExit) : mp_fOnExit(fg_Move(_fOnExit)) {}
  ~TCOnScopeExit() { mp_fOnExit(); }
};
template <class tf_FCleanup>
TCOnScopeExit<tf_FCleanup> fg_OnScopeExit(tf_FCleanup &&_fCleanup) {
  return TCOnScopeExit<tf_FCleanup>(fg_Move(_fCleanup));
}
struct CResource {
  TCFuture<int> f_Destroy();
};
// Also a guard made by the function.
TCFuture<int> fg_Work(CResource Resource) {
  auto Cleanup = fg_OnScopeExit([&] { Resource.f_Destroy(); });
// CHECK-MESSAGES: :[[@LINE-1]]:18: warning: the teardown of an actor or functor in a coroutine uses 'fg_AsyncDestroy' instead of 'fg_OnScopeExit' [malterlib-async-destroy]
  co_return 0;
}
}

namespace NRound217 {
TCFuture<int> fg_Suspend();
struct CStr {
  char m_Data[4] = "abc";
  char *m_pOther = nullptr;
  char const *f_GetStr() const { return m_Data; }
  char const &f_GetFirst() const { return m_Data[0]; }
  char const *f_GetOther() const { return m_pOther; }
};
// What a getter gives of what is captured by reference can be gone after the
// cleanup suspends.
TCFuture<int> fg_Getter() {
  CStr Name;
  auto Cleanup = co_await fg_AsyncDestroy(
      [&Name]() -> TCUnsafeFuture<int> {
        auto pText = Name.f_GetStr();
        co_await fg_Suspend();
        co_return pText[0];
// CHECK-MESSAGES: :[[@LINE-1]]:19: warning: the cleanup function of 'fg_AsyncDestroy' uses 'pText', which it captures by reference, after its first suspension, where the reference is gone; copy or move it into a local before the suspension [malterlib-async-destroy]
      });
  co_return 0;
}
TCFuture<int> fg_ReferenceGetter() {
  CStr Name;
  auto Cleanup = co_await fg_AsyncDestroy(
      [&Name]() -> TCUnsafeFuture<int> {
        auto const &First = Name.f_GetFirst();
        co_await fg_Suspend();
        co_return First;
// CHECK-MESSAGES: :[[@LINE-1]]:19: warning: the cleanup function of 'fg_AsyncDestroy' uses 'First', which it captures by reference, after its first suspension, where the reference is gone; copy or move it into a local before the suspension [malterlib-async-destroy]
      });
  co_return 0;
}
// Not a pointer it holds, which points elsewhere.
TCFuture<int> fg_OtherGetter() {
  CStr Name;
  auto Cleanup = co_await fg_AsyncDestroy(
      [&Name]() -> TCUnsafeFuture<int> {
        auto pText = Name.f_GetOther();
        co_await fg_Suspend();
        co_return pText[0];
      });
  co_return 0;
}
template <typename t_CType>
t_CType const &fg_Const(t_CType const &_Value) {
  return _Value;
}
int g_Value = 42;
// A reference to what has static storage, through an identity helper.
TCFuture<int> fg_StaticThroughConst() {
  auto const &Value = fg_Const(g_Value);
  auto Cleanup = co_await fg_AsyncDestroy([&Value]() -> TCFuture<int> {
    co_await fg_Suspend();
    co_return Value;
  });
  co_return 0;
}
TCFuture<int> fg_LocalThroughConst() {
  int Local = 42;
  auto const &Value = fg_Const(Local);
  auto Cleanup = co_await fg_AsyncDestroy([&Value]() -> TCFuture<int> {
    co_await fg_Suspend();
    co_return Value;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: the cleanup function of 'fg_AsyncDestroy' uses 'Value', which it captures by reference, after its first suspension, where the reference is gone; copy or move it into a local before the suspension [malterlib-async-destroy]
  });
  co_return 0;
}
}

namespace NRound221 {
// What a member helper whose body can be seen tears down asynchronously, also
// when it gives something else.
template <class t_FSignature>
struct TCActorFunctor {
  TCFuture<int> f_Destroy() &&;
};
struct CSubscription {
  TCActorFunctor<int()> m_Callback;
  bool f_TryDestroy() {
    (void)fg_Move(m_Callback).f_Destroy();
    return true;
  }
  bool f_Check() { return true; }
  virtual bool f_Virtual();
};
TCFuture<int> fg_Helper() {
  CSubscription Subscription;
  auto Cleanup = g_OnScopeExit / [&] {
// CHECK-MESSAGES: :[[@LINE-1]]:18: warning: the teardown of an actor or functor in a coroutine uses 'fg_AsyncDestroy' instead of 'g_OnScopeExit' [malterlib-async-destroy]
    (void)Subscription.f_TryDestroy();
  };
  co_return 0;
}
TCFuture<int> fg_Plain() {
  CSubscription Subscription;
  auto Cleanup = g_OnScopeExit / [&] {
    (void)Subscription.f_Check();
    (void)Subscription.f_Virtual();
  };
  co_return 0;
}
}

namespace NRound221Sync {
template <class t_FSignature>
struct TCActorFunctor {
  TCFuture<int> f_Destroy() &&;
};
// An `f_Destroy` that returns nothing is synchronous, whatever it starts.
struct CState {
  TCActorFunctor<int()> m_Callback;
  void f_Destroy() { (void)fg_Move(m_Callback).f_Destroy(); }
};
TCFuture<int> fg_SyncDestroy() {
  CState State;
  auto Cleanup = g_OnScopeExit / [&] { State.f_Destroy(); };
  co_return 0;
}
}

namespace NRound224 {
TCFuture<int> fg_Suspend();
// A getter that gives what another getter of the object gives.
struct CStr {
  char m_Data[8] = "Name";
  char const *fp_GetStr() const { return m_Data; }
  char const *f_GetStr() const { return fp_GetStr(); }
};
struct CString {
  CStr m_Imp;
  char const *f_GetStr() const { return m_Imp.f_GetStr(); }
};
TCFuture<int> fg_Forwarded() {
  CStr Name;
  auto Cleanup = co_await fg_AsyncDestroy([&Name]() -> TCUnsafeFuture<int> {
    auto pName = Name.f_GetStr();
    co_await fg_Suspend();
    co_return pName[0];
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: the cleanup function of 'fg_AsyncDestroy' uses 'pName', which it captures by reference, after its first suspension, where the reference is gone; copy or move it into a local before the suspension [malterlib-async-destroy]
  });
  co_return 0;
}
TCFuture<int> fg_ForwardedMember() {
  CString Name;
  auto Cleanup = co_await fg_AsyncDestroy([&Name]() -> TCUnsafeFuture<int> {
    auto pName = Name.f_GetStr();
    co_await fg_Suspend();
    co_return pName[0];
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: the cleanup function of 'fg_AsyncDestroy' uses 'pName', which it captures by reference, after its first suspension, where the reference is gone; copy or move it into a local before the suspension [malterlib-async-destroy]
  });
  co_return 0;
}
// A cleanup run before a later local is gone, or a pointer pointed
// elsewhere first, uses nothing that is gone.
TCFuture<int> fg_RunEarly() {
  int *pValue = nullptr;
  auto Cleanup = co_await fg_AsyncDestroy([&pValue]() -> TCFuture<int> {
    co_return pValue ? *pValue : 0;
  });
  int Late = 42;
  pValue = &Late;
  co_await fg_Move(Cleanup);
  co_return 0;
}
TCFuture<int> fg_PointedBack() {
  int *pValue = nullptr;
  auto Cleanup = co_await fg_AsyncDestroy([&pValue]() -> TCFuture<int> {
    co_return pValue ? *pValue : 0;
  });
  int Late = 42;
  pValue = &Late;
  pValue = nullptr;
  co_return 0;
}
// Not when something can leave between.
TCFuture<int> fg_LeavesFirst(bool _bLeave) {
  int *pValue = nullptr;
  auto Cleanup = co_await fg_AsyncDestroy([&pValue]() -> TCFuture<int> {
    co_return pValue ? *pValue : 0;
  });
  int Late = 42;
  pValue = &Late;
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: 'pValue', which the cleanup of 'fg_AsyncDestroy' uses, is pointed at 'Late', which is destroyed before the cleanup runs; point it at what is declared before the result of 'fg_AsyncDestroy' [malterlib-async-destroy]
  if (_bLeave)
    co_return 1;
  co_await fg_Move(Cleanup);
  co_return 0;
}
// A lambda that is only called where it never runs assigns nothing.
TCFuture<int> fg_NeverCalled() {
  int *pValue = nullptr;
  auto Cleanup = co_await fg_AsyncDestroy([&pValue]() -> TCFuture<int> {
    co_return pValue ? *pValue : 0;
  });
  int Late = 42;
  auto fSet = [&] { pValue = &Late; };
  if constexpr (false)
    fSet();
  auto fOuter = [&] { fSet(); };
  co_return 0;
}
}

namespace NRound228 {
// What a static member helper whose body can be seen tears down, and the
// guards the shared helper and its function make.
template <class t_FSignature>
struct TCActorFunctor {
  TCFuture<int> f_Destroy() &&;
};
struct CSubscription {
  static TCFuture<int> fs_Destroy(TCActorFunctor<int()> &_Callback) {
    return fg_Move(_Callback).f_Destroy();
  }
  static bool fs_Check(TCActorFunctor<int()> &_Callback) { return true; }
};
TCFuture<int> fg_Static() {
  TCActorFunctor<int()> Callback;
  auto Cleanup = g_OnScopeExit / [&] {
// CHECK-MESSAGES: :[[@LINE-1]]:18: warning: the teardown of an actor or functor in a coroutine uses 'fg_AsyncDestroy' instead of 'g_OnScopeExit' [malterlib-async-destroy]
    (void)CSubscription::fs_Destroy(Callback);
  };
  co_return 0;
}
TCFuture<int> fg_StaticCheck() {
  TCActorFunctor<int()> Callback;
  auto Cleanup = g_OnScopeExit / [&] {
    (void)CSubscription::fs_Check(Callback);
  };
  co_return 0;
}
struct COnScopeExitShared {
  ~COnScopeExitShared();
};
template <class tf_FCleanup>
COnScopeExitShared fg_OnScopeExitShared(tf_FCleanup &&_fCleanup) {
  return {};
}
TCFuture<int> fg_SharedFunction(TCActorFunctor<int()> _Functor) {
  auto Cleanup = fg_OnScopeExitShared([Functor = fg_Move(_Functor)]() mutable {
// CHECK-MESSAGES: :[[@LINE-1]]:18: warning: the teardown of an actor or functor in a coroutine uses 'fg_AsyncDestroy' instead of 'fg_OnScopeExitShared' [malterlib-async-destroy]
    (void)fg_Move(Functor).f_Destroy();
  });
  co_return 0;
}
TCFuture<int> fg_SharedHelper(TCActorFunctor<int()> _Functor) {
  auto Cleanup = g_OnScopeExitShared / [Functor = fg_Move(_Functor)]() mutable {
// CHECK-MESSAGES: :[[@LINE-1]]:18: warning: the teardown of an actor or functor in a coroutine uses 'fg_AsyncDestroy' instead of 'g_OnScopeExitShared' [malterlib-async-destroy]
    (void)fg_Move(Functor).f_Destroy();
  };
  co_return 0;
}
}

namespace NRound228Wrapper {
// A local function wrapper moved into the guard holds the lambda it is made
// with.
template <class t_FSignature>
struct TCActorFunctor {
  TCFuture<int> f_Destroy() &&;
};
TCFuture<int> fg_Wrapped(TCActorFunctor<int()> _Functor) {
  NMib::NFunction::TCFunction<void()> fCleanup = [&] {
    (void)fg_Move(_Functor).f_Destroy();
  };
  auto Cleanup = g_OnScopeExit / fg_Move(fCleanup);
// CHECK-MESSAGES: :[[@LINE-1]]:18: warning: the teardown of an actor or functor in a coroutine uses 'fg_AsyncDestroy' instead of 'g_OnScopeExit' [malterlib-async-destroy]
  co_return 0;
}
}

namespace NRound229 {
// A pointer pointed elsewhere on the path that suspends isn't gone where the
// paths meet, though the other still points into what is captured.
TCFuture<int> fg_Suspend();
struct CStr {
  int m_Length = 0;
};
void fg_Consume(int _Value);
TCFuture<int> fg_Cleanup(bool _bAsync) {
  CStr Text;
  auto Cleanup = co_await fg_AsyncDestroy([&Text, _bAsync]() -> TCFuture<int> {
    CStr Copy;
    CStr const *pText = &Text;
    if (_bAsync) {
      Copy = Text;
      pText = &Copy;
      co_await fg_Suspend();
    }
    fg_Consume(pText->m_Length);
    co_return 0;
  });
  co_return 0;
}
TCFuture<int> fg_CleanupGone(bool _bAsync) {
  CStr Text;
  auto Cleanup = co_await fg_AsyncDestroy([&Text, _bAsync]() -> TCFuture<int> {
    CStr Copy;
    CStr const *pText = &Text;
    if (_bAsync) {
      Copy = Text;
      co_await fg_Suspend();
    }
    fg_Consume(pText->m_Length);
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: the cleanup function of 'fg_AsyncDestroy' uses 'pText', which it captures by reference, after its first suspension, where the reference is gone; copy or move it into a local before the suspension [malterlib-async-destroy]
    co_return 0;
  });
  co_return 0;
}
}

namespace NRound231 {
// Pointed at what a function gives back of a local it is given by reference,
// `&fg_Const(Local)`, it points at the local; of a constant conditional, at
// what it chooses.
template <class t_CType>
constexpr t_CType const &fg_Const(t_CType const &_Value) noexcept {
  return (t_CType const &)_Value;
}
TCFuture<int> fg_Const() {
  int const *pValue = nullptr;
  auto Guard = co_await fg_AsyncDestroy([&pValue]() -> TCFuture<int> {
    co_return *pValue;
  });
  {
    int Local = 42;
    pValue = &fg_Const(Local);
// CHECK-MESSAGES: :[[@LINE-1]]:5: warning: 'pValue', which the cleanup of 'fg_AsyncDestroy' uses, is pointed at 'Local', which is destroyed before the cleanup runs; point it at what is declared before the result of 'fg_AsyncDestroy' [malterlib-async-destroy]
  }
  co_return 0;
}
template <bool t_bKeepOriginal>
TCFuture<int> fg_Chosen() {
  int Value = 42;
  int const *pValue = &Value;
  auto Guard = co_await fg_AsyncDestroy([&pValue]() -> TCFuture<int> {
    co_return *pValue;
  });
  int Unused = 7;
  pValue = t_bKeepOriginal ? &Value : &Unused;
  co_return 0;
}
void fg_Instantiate() { fg_Chosen<true>(); }
}

namespace NRound233 {
// What a lambda `fg_CallSafe` calls tears down, which it calls right away.
template <class t_FSignature>
struct TCActorFunctor {
  TCFuture<int> f_Destroy() &&;
};
TCFuture<int> fg_Run(TCActorFunctor<int()> _Functor) {
  auto Cleanup = g_OnScopeExit / [Functor = fg_Move(_Functor)]() mutable {
// CHECK-MESSAGES: :[[@LINE-1]]:18: warning: the teardown of an actor or functor in a coroutine uses 'fg_AsyncDestroy' instead of 'g_OnScopeExit' [malterlib-async-destroy]
    fg_CallSafe([Functor = fg_Move(Functor)]() mutable -> TCFuture<int> {
      co_await fg_Move(Functor).f_Destroy();
      co_return 0;
    });
  };
  co_return 0;
}
}

namespace NRound234 {
TCFuture<int> fg_Suspend();
// A copy of a captured pointer to a local, which is destroyed while it
// suspends.
TCFuture<int> fg_Run() {
  int Value = 0;
  int *pValue = &Value;
  auto Cleanup = co_await fg_AsyncDestroy([&pValue]() -> TCUnsafeFuture<int> {
    int *pSaved = pValue;
    co_await fg_Suspend();
    co_return *pSaved;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: the cleanup function of 'fg_AsyncDestroy' uses 'pSaved', which it captures by reference, after its first suspension, where the reference is gone; copy or move it into a local before the suspension [malterlib-async-destroy]
  });
  co_return 0;
}
}

namespace NRound236 {
template <typename t_FSignature>
struct TCDestroyable {
  TCFuture<int> f_Destroy() &&;
};
using CCallback = TCDestroyable<TCFuture<int>()>;
// Torn down by a scope exit made in a scope exit, which runs it.
TCFuture<int> fg_Nested(CCallback _Callback) {
  auto Cleanup = g_OnScopeExit / [&] {
// CHECK-MESSAGES: :[[@LINE-1]]:18: warning: the teardown of an actor or functor in a coroutine uses 'fg_AsyncDestroy' instead of 'g_OnScopeExit' [malterlib-async-destroy]
    auto Inner = g_OnScopeExit / [&] { fg_Move(_Callback).f_Destroy(); };
  };
  co_return 0;
}
// Not in the value a constant condition doesn't choose.
template <bool t_bOwnCallback>
TCFuture<int> fg_Chosen(CCallback _Callback) {
  auto Cleanup = g_OnScopeExit / [&] {
    t_bOwnCallback ? fg_Move(_Callback).f_Destroy() : TCFuture<int>{};
  };
  co_return 0;
}
void fg_Use(CCallback _Callback) { fg_Chosen<false>(fg_Move(_Callback)); }
}

namespace NRound243 {
struct CValue {
  int m_Value = 0;
  ~CValue();
};
// A setter that points it at what it is given through a local, and one given
// a reference to what is declared before the result.
void fg_SetThrough(CValue *&o_pValue, CValue &_Value) {
  CValue *pNew = &_Value;
  o_pValue = pNew;
}
void fg_Set(CValue *&o_pValue, CValue &_Value) { o_pValue = &_Value; }
TCFuture<int> fg_Through() {
  CValue Early;
  CValue *pValue = &Early;
  auto Cleanup = co_await fg_AsyncDestroy([&pValue]() -> TCFuture<int> {
    co_return pValue->m_Value;
  });
  CValue Late;
  fg_SetThrough(pValue, Late);
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: 'pValue', which the cleanup of 'fg_AsyncDestroy' uses, is pointed at 'Late', which is destroyed before the cleanup runs; point it at what is declared before the result of 'fg_AsyncDestroy' [malterlib-async-destroy]
  co_return 0;
}
TCFuture<int> fg_Alias() {
  CValue Early;
  CValue *pValue = &Early;
  auto Cleanup = co_await fg_AsyncDestroy([&pValue]() -> TCFuture<int> {
    co_return pValue->m_Value;
  });
  CValue &Alias = Early;
  fg_Set(pValue, Alias);
  co_return 0;
}
}
