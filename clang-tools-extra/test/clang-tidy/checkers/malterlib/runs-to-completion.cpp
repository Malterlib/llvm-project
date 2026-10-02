// RUN: %check_clang_tidy -std=c++20-or-later %s malterlib-runs-to-completion,malterlib-coroutine-reference-capture,malterlib-coroutine-reference-parameter,malterlib-coroutine-unsafe-this %t -- -config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" -- -I %S/Inputs

#include "malterlib-runs-to-completion.h"
#include "malterlib-actor-phase.h"

struct CState
{
	int m_Value = 0;
};

TCFuture<int> fg_Work(int _Value);

// A function that runs to completion may refer to the data of its callers,
// which join it.
DMibRunsToCompletion TCFuture<int> fg_Update(CState &_State, CState const *_pOther)
{
	co_await fg_Work(0);
	_State.m_Value = _pOther->m_Value;

	TCFutureVector<int> Results;
	auto JoinResults = co_await fg_AsyncDestroy
		(
			[&]() -> TCFuture<int>
			{
				co_await fg_AllDone(Results);
				co_return 0;
			}
		)
	;
	int Local = 0;
	g_Dispatch / [&_State, &Local]() -> TCFuture<int>
		{
			co_await fg_Work(0);
			co_return _State.m_Value + Local;
		}
		> Results
	;
// CHECK-MESSAGES: :[[@LINE-7]]:26: warning: a TCFuture coroutine lambda captures 'Local' by reference but is not called directly where 'Local' is, {{.*}}
	co_await
		(
			g_Dispatch / [&Local]() -> TCFuture<int>
			{
				co_await fg_Work(0);
				co_return Local;
			}
		)
	;
	co_return 0;
}

// A function that runs to completion that isn't joined, or is called where
// the code can be aborted.
DMibRunsToCompletion TCFuture<int> fg_Callers(CState &_State)
{
	co_await fg_Update(_State, &_State);
	fg_Update(_State, &_State);
// CHECK-MESSAGES: :[[@LINE-1]]:2: warning: 'fg_Update' runs to completion, but isn't joined here, {{.*}}
	auto pFunction = &fg_Update;
// CHECK-MESSAGES: :[[@LINE-1]]:20: warning: 'fg_Update' runs to completion, but is used here without being called, {{.*}}
	(void)pFunction;

	TCFutureVector<int> Results;
	CState Early;
	auto JoinResults = co_await fg_AsyncDestroy
		(
			[&]() -> TCFuture<int>
			{
				co_await fg_AllDone(Results);
				co_return 0;
			}
		)
	;
	fg_Update(Early, &_State) > Results;
	{
		CState Late;
		fg_Update(Late, &Early) > Results;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'fg_Update' runs to completion and is joined by 'JoinResults', but this argument may not live until then; {{.*}}
	}
	// The guard is joined where the coroutine finishes, after its locals are
	// gone, unless it is awaited early, while they are there.
	co_await fg_Move(JoinResults);
	co_return 0;
}

DMibRunsToCompletion TCFuture<int> fg_NotAwaitedEarly(CState &_State)
{
	TCFutureVector<int> Results;
	CState Local;
	auto JoinResults = co_await fg_AsyncDestroy
		(
			[&]() -> TCFuture<int>
			{
				co_await fg_AllDone(Results);
				co_return 0;
			}
		)
	;
	fg_Update(_State, &_State) > Results;
	fg_Update(Local, &_State) > Results;
// CHECK-MESSAGES: :[[@LINE-1]]:12: warning: 'fg_Update' runs to completion and is joined by 'JoinResults', but this argument may not live until then; {{.*}}
	co_return 0;
}

TCFuture<int> fg_NotRunningToCompletion(CState &_State)
// CHECK-MESSAGES: :[[@LINE-1]]:49: warning: TCFuture coroutine 'fg_NotRunningToCompletion' uses '_State' after a suspension, {{.*}}
{
	co_await fg_Work(0);
	co_await fg_Update(_State, &_State);
// CHECK-MESSAGES: :[[@LINE-1]]:11: warning: 'fg_Update' runs to completion, but is called here where the code doesn't, {{.*}}
	co_return 0;
}

// A class whose member functions run to completion: its object is like a
// reference parameter.
struct DMibRunsToCompletion CBuildSystem
{
	TCFuture<int> f_Action()
	{
		co_await fg_Work(0);
		co_await f_Other();
		co_await
			(
				g_Dispatch / [this]() -> TCFuture<int>
				{
					co_await fg_Work(0);
					co_return m_Value;
				}
			)
		;
		co_return m_Value;
	}

	TCFuture<int> f_Other()
	{
		co_await fg_Work(0);
		co_await fg_ContinueRunningOnActor(m_Actor);
// CHECK-MESSAGES: :[[@LINE-1]]:12: warning: code that runs to completion switches here to an actor that may be destroyed while it runs, {{.*}}
		co_await fg_ContinueRunningOnActor(fg_ConcurrentActor());
		co_return m_Value;
	}

	int m_Value = 0;
	TCActor<> m_Actor;
};

// An entry switches to a concurrent actor before it suspends, and runs the
// lambdas given to it in its context.
DMibRunsToCompletionEntry TCFuture<int> fg_Run(TCFunction<TCFuture<int> (CBuildSystem *)> _fCommand)
{
	co_await fg_ContinueRunningOnActor(fg_ConcurrentActorLowPrio());
	CBuildSystem BuildSystem;
	co_await _fCommand(&BuildSystem);
	co_return 0;
}

DMibRunsToCompletionEntry TCFuture<int> fg_RunForward(TCFunction<TCFuture<int> (CBuildSystem *)> _fCommand)
{
	co_await fg_Run(fg_Move(_fCommand));
	co_return 0;
}

DMibRunsToCompletionEntry TCFuture<int> fg_RunLate()
{
	co_await fg_Work(0);
// CHECK-MESSAGES: :[[@LINE-1]]:2: warning: 'fg_RunLate' begins to run to completion, but suspends here before it switches to a concurrent actor on every path; {{.*}}
	co_return 0;
}

TCFuture<int> fg_Command()
{
	co_await fg_RunForward
		(
			[](CBuildSystem *_pBuildSystem) -> TCFuture<int>
			{
				co_return co_await _pBuildSystem->f_Action();
			}
		)
	;
	CBuildSystem BuildSystem;
	co_await BuildSystem.f_Action();
// CHECK-MESSAGES: :[[@LINE-1]]:11: warning: 'f_Action' runs to completion, but is called here where the code doesn't, {{.*}}
	co_return 0;
}

// A class derived from one that runs to completion runs to completion too, and
// a lambda held in a local whose calls are all awaited runs in its context.
struct CGenerator : CBuildSystem
{
	TCFuture<int> f_Generate()
	{
		co_await f_Action();
		auto fApply = [&](int _Value) -> TCFuture<int>
			{
				co_await fg_Work(_Value);
				co_return co_await f_Other();
			}
		;
		co_await fApply(1);
		co_await fApply(2);
		auto fLater = [&](int _Value) -> TCFuture<int>
			{
				co_return co_await f_Other();
// CHECK-MESSAGES: :[[@LINE-1]]:24: warning: 'f_Other' runs to completion, but is called here where the code doesn't, {{.*}}
			}
		;
		fLater(1);
		co_return 0;
	}
};

// An entry may first await another entry, also through `.f_Wrap()`.
struct CWrapped
{
	TCFuture<int> f_Wrap();
};
DMibRunsToCompletionEntry CWrapped fg_RunWrapped();

DMibRunsToCompletionEntry TCFuture<int> fg_RunThroughWrap()
{
	co_await fg_RunWrapped().f_Wrap();
	co_return 0;
}

// A cleanup made by a factory, used in a lambda that is awaited: in code that
// runs to completion, what the functions around it have lives until it ran.
TCUnsafeFuture<int> fg_FactoryNotRunningToCompletion(CState const &_State)
{
	auto fMakeHook = [&](int const &_Location)
		{
			return [&]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:12: warning: a TCFuture coroutine lambda captures '_State' by reference but is not called directly where '_State' is, {{.*}}
				{
					CState Local = _State;
					co_await fg_Work(_Location);
					co_return Local.m_Value;
				}
			;
		}
	;
	auto fInner = [&]() -> TCUnsafeFuture<int>
		{
			int Location = 1;
			auto Restore = co_await fg_AsyncDestroy(fMakeHook(Location));
			co_return 0;
		}
	;
	co_await fInner();
	co_return 0;
}

DMibRunsToCompletion TCUnsafeFuture<int> fg_FactoryRunningToCompletion(CState const &_State)
{
	auto fMakeHook = [&](int const &_Location)
		{
			return [&]() -> TCFuture<int>
				{
					CState Local = _State;
					co_await fg_Work(_Location);
					co_return Local.m_Value;
				}
			;
		}
	;
	auto fInner = [&]() -> TCUnsafeFuture<int>
		{
			int Location = 1;
			auto Restore = co_await fg_AsyncDestroy(fMakeHook(Location));
			co_return 0;
		}
	;
	co_await fInner();
	co_return 0;
}

// An entry that switches only on some paths, a cleanup that joins only on some
// paths or is cleared, the object of an operator, and what a pointer points to.
DMibRunsToCompletionEntry TCFuture<int> fg_ConditionalSwitch(bool _bSwitch)
{
	if (_bSwitch)
		co_await fg_ContinueRunningOnActor(fg_ConcurrentActor());
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: 'fg_ConditionalSwitch' begins to run to completion, but suspends here before it switches to a concurrent actor on every path; {{.*}}
	co_await fg_Work(0);
	co_return 0;
}

DMibRunsToCompletion TCFuture<int> fg_Read(int const &_Value);

DMibRunsToCompletion TCFuture<int> fg_BadGuards(bool _bJoin)
{
	int Value = 0;
	TCFutureVector<int> Results;
	auto Conditional = co_await fg_AsyncDestroy
		(
			[&]() -> TCFuture<int>
			{
				if (_bJoin)
					co_await fg_AllDone(Results);
				co_return 0;
			}
		)
	;
	fg_Read(Value) > Results;
// CHECK-MESSAGES: :[[@LINE-1]]:2: warning: 'fg_Read' runs to completion, but isn't joined here, {{.*}}
	TCFutureVector<int> Cleared;
	auto Guard = co_await fg_AsyncDestroy
		(
			[&]() -> TCFuture<int>
			{
				co_await fg_AllDone(Cleared);
				co_return 0;
			}
		)
	;
	Guard.f_Clear();
	fg_Read(Value) > Cleared;
// CHECK-MESSAGES: :[[@LINE-1]]:2: warning: 'fg_Read' runs to completion, but isn't joined here, {{.*}}
	co_return 0;
}

struct DMibRunsToCompletion CFunctor
{
	TCFuture<int> operator () ()
	{
		co_await fg_Work(0);
		co_return m_Value;
	}
	int m_Value = 0;
};

DMibRunsToCompletion TCFuture<int> fg_Receivers()
{
	TCFutureVector<int> Results;
	int *pValue = nullptr;
	auto JoinResults = co_await fg_AsyncDestroy
		(
			[&]() -> TCFuture<int>
			{
				co_await fg_AllDone(Results);
				co_return 0;
			}
		)
	;
	CFunctor{}() > Results;
// CHECK-MESSAGES: :[[@LINE-1]]:2: warning: 'operator()' runs to completion and is joined by 'JoinResults', but this argument may not live until then; {{.*}}
	{
		int Inner = 1;
		pValue = &Inner;
		fg_Read(*pValue) > Results;
// CHECK-MESSAGES: :[[@LINE-1]]:11: warning: 'fg_Read' runs to completion and is joined by 'JoinResults', but this argument may not live until then; {{.*}}
	}
	co_return 0;
}

// A cleanup made by a factory can't refer to the factory's locals.
DMibRunsToCompletion TCFuture<int> fg_FactoryLocal()
{
	auto fMake = []
		{
			int Local = 1;
			return [&Local]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: a TCFuture coroutine lambda captures 'Local' by reference {{.*}}
				{
					int Value = Local;
					co_await fg_Work(Value);
					co_return Value;
				}
			;
		}
	;
	auto Guard = co_await fg_AsyncDestroy(fMake());
	co_return 0;
}

// A local of the function around a joined lambda lives until the lambda's own
// joins; `% "Context"` forwards an entry; calls in templates are checked in
// their instantiations.
DMibRunsToCompletion TCFuture<int> fg_Nested()
{
	int Value = 0;
	co_await
		(
			g_Dispatch / [&Value]() -> TCFuture<int>
			{
				TCFutureVector<int> Results;
				auto JoinResults = co_await fg_AsyncDestroy
					(
						[&]() -> TCFuture<int>
						{
							co_await fg_AllDone(Results);
							co_return 0;
						}
					)
				;
				fg_Read(Value) > Results;
				co_return 0;
			}
		)
	;
	co_return 0;
}

DMibRunsToCompletionEntry TCFuture<int> fg_ForwardContext()
{
	co_await (fg_RunLate() % "Context");
	co_return 0;
}

template <typename t_CValue>
TCFuture<int> fg_TemplateCaller(t_CValue const &_Value)
// CHECK-MESSAGES: :[[@LINE-1]]:49: warning: TCFuture coroutine 'fg_TemplateCaller' uses '_Value' after a suspension, {{.*}}
{
	co_await fg_Work(0);
	co_return co_await fg_Read(_Value);
// CHECK-MESSAGES: :[[@LINE-1]]:21: warning: 'fg_Read' runs to completion, but is called here where the code doesn't, {{.*}}
}

template <typename t_CValue>
DMibRunsToCompletionEntry TCFuture<int> fg_TemplateEntry(t_CValue _Value)
{
	co_return co_await fg_ForwardContext();
}

void fg_InstantiateTemplates()
{
	fg_TemplateCaller(1);
	fg_TemplateEntry(1);
}

// Round 22: pointer arguments, constructors that aren't copies, cleanups that
// throw or suspend first, replaced collections, nested awaits in entries,
// local classes, other member calls, and reference init captures.
DMibRunsToCompletion TCFuture<int> fg_ReadPointer(int const *_pValue);

struct CDiscard
{
	CDiscard(TCFuture<int> &&_Future);
	bool await_ready() const noexcept;
	void await_suspend(std::coroutine_handle<>) const noexcept;
	int await_resume() const noexcept;
};

DMibRunsToCompletion TCFuture<int> fg_Round22()
{
	TCFutureVector<int> Results;
	int *pValue = nullptr;
	int Value = 0;
	auto JoinResults = co_await fg_AsyncDestroy
		(
			[&]() -> TCFuture<int>
			{
				co_await fg_AllDone(Results);
				co_return 0;
			}
		)
	;
	{
		int Inner = 1;
		pValue = &Inner;
		fg_ReadPointer(pValue) > Results;
// CHECK-MESSAGES: :[[@LINE-1]]:18: warning: 'fg_ReadPointer' runs to completion and is joined by 'JoinResults', but this argument may not live until then; {{.*}}
	}
	co_await CDiscard(fg_Read(Value));
// CHECK-MESSAGES: :[[@LINE-1]]:20: warning: 'fg_Read' runs to completion, but isn't joined here, {{.*}}
	g_Dispatch / [&Alias = Value]() -> TCFuture<int>
		{
			co_await fg_Work(0);
			co_return Alias;
		}
		> Results
	;
	co_await fg_Move(JoinResults);
	co_return 0;
}

DMibRunsToCompletion TCFuture<int> fg_BadCleanups()
{
	int Value = 1;
	TCFutureVector<int> Throws;
	auto ThrowGuard = co_await fg_AsyncDestroy
		(
			[&]() -> TCFuture<int>
			{
				throw 1;
				co_await fg_AllDone(Throws);
				co_return 0;
			}
		)
	;
	fg_Read(Value) > Throws;
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: 'fg_Read' runs to completion and is joined by 'ThrowGuard', but this argument may not live until then; {{.*}}
	TCFutureVector<int> Replaced;
	auto ReplacedGuard = co_await fg_AsyncDestroy
		(
			[&]() -> TCFuture<int>
			{
				co_await fg_AllDone(Replaced);
				co_return 0;
			}
		)
	;
	fg_Read(Value) > Replaced;
// CHECK-MESSAGES: :[[@LINE-1]]:2: warning: 'fg_Read' runs to completion, but isn't joined here, {{.*}}
	Replaced = {};
	co_return 0;
}

DMibRunsToCompletionEntry TCFuture<int> fg_NestedFirst()
{
	co_return co_await fg_TemplateEntry(co_await fg_Work(0));
// CHECK-MESSAGES: :[[@LINE-1]]:38: warning: 'fg_NestedFirst' begins to run to completion, but suspends here before it switches to a concurrent actor on every path; {{.*}}
}

DMibRunsToCompletionEntry TCFuture<int> fg_LocalClass()
{
	struct CHelper
	{
		TCFuture<int> f_Run()
		{
			co_await fg_ContinueRunningOnActor(fg_ConcurrentActor());
			co_return 0;
		}
	};
	co_await fg_Work(0);
// CHECK-MESSAGES: :[[@LINE-1]]:2: warning: 'fg_LocalClass' begins to run to completion, but suspends here before it switches to a concurrent actor on every path; {{.*}}
	co_return 0;
}

struct CEntryResult
{
	TCFuture<int> f_Other();
};
DMibRunsToCompletionEntry CEntryResult fg_StartEntry();

DMibRunsToCompletionEntry TCFuture<int> fg_OtherMember()
{
	co_await fg_StartEntry().f_Other();
// CHECK-MESSAGES: :[[@LINE-1]]:2: warning: 'fg_OtherMember' begins to run to completion, but suspends here before it switches to a concurrent actor on every path; {{.*}}
	co_return 0;
}

// Round 23: reference aliases of pointers, views, slots assigned, evaluation
// order of assignments, jumps, array elements, member init captures, and
// static operators.
struct CStrPtr
{
	char const *m_pStr;
};

DMibRunsToCompletion TCFuture<int> fg_ReadView(CStrPtr _View);

struct CFutureSlots
{
	TCFuture<int> &operator [] (int _Key);
};
TCFuture<int> fg_AllDone(CFutureSlots &_Slots);

struct DMibRunsToCompletion CStaticFunctor
{
	static TCFuture<int> operator () (int _Value)
	{
		co_await fg_Work(0);
		co_return _Value;
	}
};

DMibRunsToCompletion TCFuture<int> fg_Round23()
{
	int *pValue = nullptr;
	int *&rpValue = pValue;
	int Values[2] = {1, 2};
	struct CState
	{
		int m_Value;
	} State{1};
	TCFutureVector<int> Results;
	auto JoinResults = co_await fg_AsyncDestroy
		(
			[&]() -> TCFuture<int>
			{
				co_await fg_AllDone(Results);
				co_return 0;
			}
		)
	;
	{
		int Inner = 1;
		pValue = &Inner;
		fg_ReadPointer(rpValue) > Results;
// CHECK-MESSAGES: :[[@LINE-1]]:18: warning: 'fg_ReadPointer' runs to completion and is joined by 'JoinResults', {{.*}}
		char Character = 'a';
		fg_ReadView(CStrPtr{&Character}) > Results;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'fg_ReadView' runs to completion and is joined by 'JoinResults', {{.*}}
	}
	fg_Read(Values[0]) > Results;
	g_Dispatch / [&Alias = State.m_Value]() -> TCFuture<int>
		{
			co_await fg_Work(0);
			co_return Alias;
		}
		> Results
	;
	CStaticFunctor{}(1) > Results;
	co_await fg_Move(JoinResults);
	co_return 0;
}

DMibRunsToCompletion TCFuture<int> fg_SlotAssigned()
{
	int Value = 1;
	CFutureSlots Slots;
	auto JoinSlots = co_await fg_AsyncDestroy
		(
			[&]() -> TCFuture<int>
			{
				co_await fg_AllDone(Slots);
				co_return 0;
			}
		)
	;
	Slots[0] = fg_Read(Value);
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'fg_Read' runs to completion, but isn't joined here, {{.*}}
	co_return 0;
}

DMibRunsToCompletionEntry TCFuture<int> fg_AssignmentOrder()
{
	int Values[1];
	Values[co_await fg_ForwardContext()] = co_await fg_Work(0);
// CHECK-MESSAGES: :[[@LINE-1]]:41: warning: 'fg_AssignmentOrder' begins to run to completion, but suspends here before it switches to a concurrent actor on every path; {{.*}}
	co_return 0;
}

DMibRunsToCompletionEntry TCFuture<int> fg_JumpsPast(bool _bSkip)
{
	if (_bSkip)
		goto Later;
	co_await fg_ContinueRunningOnActor(fg_ConcurrentActor());
// CHECK-MESSAGES: :[[@LINE-1]]:2: warning: 'fg_JumpsPast' begins to run to completion, but suspends here before it switches to a concurrent actor on every path; {{.*}}
Later:
	co_await fg_Work(0);
	co_return 0;
}

// Round 24: a suspension in a capture, a switch in an expression whose other
// operands can be evaluated first, and the object of a call through a pointer.
DMibRunsToCompletionEntry TCFuture<int> fg_CaptureSuspends()
{
	auto fGet = [Value = co_await fg_Work(0)] { return Value; };
// CHECK-MESSAGES: :[[@LINE-1]]:23: warning: 'fg_CaptureSuspends' begins to run to completion, but suspends here before it switches to a concurrent actor on every path; {{.*}}
	co_await fg_ContinueRunningOnActor(fg_ConcurrentActor());
	co_return fGet();
}

DMibRunsToCompletionEntry TCFuture<int> fg_Unsequenced()
{
	int Value = (co_await fg_ContinueRunningOnActor(fg_ConcurrentActor()), 0) + co_await fg_Work(0);
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'fg_Unsequenced' begins to run to completion, but suspends here before it switches to a concurrent actor on every path; {{.*}}
	co_return Value;
}

DMibRunsToCompletion TCFuture<int> fg_ThroughPointer()
{
	CBuildSystem *pBuildSystem = nullptr;
	TCFutureVector<int> Results;
	auto JoinResults = co_await fg_AsyncDestroy
		(
			[&]() -> TCFuture<int>
			{
				co_await fg_AllDone(Results);
				co_return 0;
			}
		)
	;
	CBuildSystem Late;
	pBuildSystem = &Late;
	pBuildSystem->f_Action() > Results;
// CHECK-MESSAGES: :[[@LINE-1]]:2: warning: 'f_Action' runs to completion and is joined by 'JoinResults', but this argument may not live until then; {{.*}}
	co_return 0;
}

// Round 25: constructor arguments, a guard awaited early, and the address of
// a local pointer.
struct CPair
{
	CPair(int _First, int _Second);
};

DMibRunsToCompletionEntry TCFuture<int> fg_ConstructorArguments()
{
	CPair Pair(co_await fg_ForwardContext(), co_await fg_Work(0));
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'fg_ConstructorArguments' begins to run to completion, but suspends here before it switches to a concurrent actor on every path; {{.*}}
	co_return 0;
}

DMibRunsToCompletion TCFuture<int> fg_Update(int **o_ppValue);

DMibRunsToCompletion TCFuture<int> fg_EarlyAwait(int &_Value)
{
	TCFutureVector<int> Results;
	int *pValue = nullptr;
	auto JoinResults = co_await fg_AsyncDestroy
		(
			[&]() -> TCFuture<int>
			{
				co_await fg_AllDone(Results);
				co_return 0;
			}
		)
	;
	fg_Read(_Value) > Results;
	fg_Update(&pValue) > Results;
	co_await fg_Move(JoinResults);
	fg_Read(_Value) > Results;
// CHECK-MESSAGES: :[[@LINE-1]]:2: warning: 'fg_Read' runs to completion, but isn't joined here, {{.*}}
	co_return 0;
}

// Round 26: a lambda held in a local called from a coroutine that can be
// aborted, a pointer kept in a member, a view local given by value, a lambda
// run through fg_CallSafe, and an explicit object parameter.
DMibRunsToCompletion TCFuture<int> fg_HeldInAbortable(TCActor<> _Other)
{
	auto fRead = [](int const &_Value) -> TCFuture<int>
		{
			co_return co_await fg_Read(_Value);
// CHECK-MESSAGES: :[[@LINE-1]]:23: warning: 'fg_Read' runs to completion, but is called here where the code doesn't, {{.*}}
		}
	;
	co_await
		(
			g_Dispatch(_Other) / [fRead]() -> TCFuture<int>
			{
				int Inner = 42;
				co_return co_await fRead(Inner);
			}
		)
	;
	co_return 0;
}

struct CPointerState
{
	int *m_pValue;
};

DMibRunsToCompletion TCFuture<int> fg_PointerMember()
{
	TCFutureVector<int> Results;
	CPointerState State{};
	CStrPtr View{nullptr};
	char Character = 'a';
	CStrPtr LiveView{&Character};
	auto JoinResults = co_await fg_AsyncDestroy
		(
			[&]() -> TCFuture<int>
			{
				co_await fg_AllDone(Results);
				co_return 0;
			}
		)
	;
	{
		int Inner = 42;
		State.m_pValue = &Inner;
		fg_ReadPointer(State.m_pValue) > Results;
// CHECK-MESSAGES: :[[@LINE-1]]:18: warning: 'fg_ReadPointer' runs to completion and is joined by 'JoinResults', {{.*}}
	}
	fg_ReadView(LiveView) > Results;
	(void)View;
	co_await fg_Move(JoinResults);
	co_return 0;
}

DMibRunsToCompletion TCFuture<int> fg_CallSafeHeld(int &_Value)
{
	auto fRead = [&_Value]() -> TCFuture<int>
		{
			co_await fg_Work(0);
			co_return _Value;
		}
	;
	co_return co_await fg_CallSafe(fRead);
}


// Round 27: a lambda that calls itself through a function wrapper, a guard
// awaited early in a loop, fg_CallSafe running only its first argument, an
// actor's class annotation, a copy of a pointer, an override of a function that
// doesn't run to completion, and a default argument.
DMibRunsToCompletion TCFuture<int> fg_Recursive()
{
	TCFunction<TCFuture<int> (int)> fRecurse = [&](int _Depth) -> TCFuture<int>
		{
			if (_Depth)
				co_return co_await fRecurse(_Depth - 1);
			co_return co_await fg_Work(_Depth);
		}
	;
	co_return co_await fRecurse(1);
}

DMibRunsToCompletion TCFuture<int> fg_LoopEarlyAwait(int &_Value)
{
	TCFutureVector<int> Results;
	auto JoinResults = co_await fg_AsyncDestroy
		(
			[&]() -> TCFuture<int>
			{
				co_await fg_AllDone(Results);
				co_return 0;
			}
		)
	;
	int Local = 0;
	for (int i = 0; i != 2; ++i)
	{
		fg_Read(Local) > Results;
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: 'fg_Read' runs to completion, but isn't joined here, {{.*}}
		co_await fg_Move(JoinResults);
	}
	co_return 0;
}

struct CCaller
{
	TCFuture<int> operator () (TCFunction<TCFuture<int> ()> _fCallback) const;
};

DMibRunsToCompletion TCFuture<int> fg_CallSafeSecond()
{
	co_await fg_CallSafe
		(
			CCaller{}
			, []() -> TCFuture<int>
			{
				int Value = 1;
				co_return co_await fg_Read(Value);
// CHECK-MESSAGES: :[[@LINE-1]]:24: warning: 'fg_Read' runs to completion, but is called here where the code doesn't, {{.*}}
			}
		)
	;
	co_return 0;
}

struct DMibRunsToCompletion CRunsActor : CActor
{
	TCFuture<int> f_Run()
	{
		int Value = 42;
		co_return co_await fg_Read(Value);
// CHECK-MESSAGES: :[[@LINE-1]]:22: warning: 'fg_Read' runs to completion, but is called here where the code doesn't, {{.*}}
	}
};

DMibRunsToCompletion TCFuture<int> fg_PointerCopy()
{
	TCFutureVector<int> Results;
	int *pEarly = nullptr;
	auto JoinResults = co_await fg_AsyncDestroy
		(
			[&]() -> TCFuture<int>
			{
				co_await fg_AllDone(Results);
				co_return 0;
			}
		)
	;
	{
		int Late = 42;
		pEarly = &Late;
		int *pCopy = pEarly;
		fg_ReadPointer(pCopy) > Results;
// CHECK-MESSAGES: :[[@LINE-1]]:18: warning: 'fg_ReadPointer' runs to completion and is joined by 'JoinResults', {{.*}}
	}
	co_return 0;
}

struct CVirtualBase
{
	virtual TCFuture<int> f_Run(int &_Value) = 0;
};

struct DMibRunsToCompletion CVirtualDerived : CVirtualBase
{
	TCFuture<int> f_Run(int &_Value) override;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'f_Run' runs to completion, but overrides 'CVirtualBase::f_Run', which doesn't, {{.*}}
};

DMibRunsToCompletion TCFuture<int> fg_ReadDefault(int const &_Value = 1);

DMibRunsToCompletion TCFuture<int> fg_DefaultArgument()
{
	TCFutureVector<int> Results;
	auto JoinResults = co_await fg_AsyncDestroy
		(
			[&]() -> TCFuture<int>
			{
				co_await fg_AllDone(Results);
				co_return 0;
			}
		)
	;
	fg_ReadDefault() > Results;
// CHECK-MESSAGES: :[[@LINE-1]]:2: warning: 'fg_ReadDefault' runs to completion and is joined by 'JoinResults', {{.*}}
	co_return 0;
}

// A guard that is returned runs its cleanup after the variables it refers to
// are gone.
DMibRunsToCompletion TCFuture<CAsyncDestroy> fg_ReturnedGuard()
{
	int Local = 42;
	co_return co_await fg_AsyncDestroy
		(
			[&Local]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:6: warning: a TCFuture coroutine lambda captures 'Local' by reference {{.*}}
			{
				co_await fg_Work(0);
				co_return Local;
			}
		)
	;
}

// What a pointer that another pointer points to points to can't be told.
DMibRunsToCompletion TCFuture<int> fg_DoubleDereference()
{
	int *pValue = nullptr;
	int **ppValue = &pValue;
	TCFutureVector<int> Results;
	auto JoinResults = co_await fg_AsyncDestroy
		(
			[&]() -> TCFuture<int>
			{
				co_await fg_AllDone(Results);
				co_return 0;
			}
		)
	;
	int Late = 42;
	pValue = &Late;
	fg_Read(**ppValue) > Results;
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: 'fg_Read' runs to completion and is joined by 'JoinResults', but this argument may not live until then; {{.*}}
	co_return 0;
}

// Switching through a pointer to the switch, to an actor that can't be told.
DMibRunsToCompletion TCFuture<int> fg_IndirectSwitch(TCActor<> _Actor)
{
	auto fSwitch = static_cast<CCoroutineTransferOwnership (*)(TCActor<> const &)>(&fg_ContinueRunningOnActor);
// CHECK-MESSAGES: :[[@LINE-1]]:82: warning: code that runs to completion uses fg_ContinueRunningOnActor here without calling it, {{.*}}
	co_await fSwitch(_Actor);
	co_return 0;
}

DMibRunsToCompletionEntry TCFuture<int> fg_FirstEntry()
{
	co_await fg_ContinueRunningOnActor(fg_ConcurrentActor());
	co_return 1;
}

// What an assignment assigns is evaluated first.
DMibRunsToCompletionEntry TCFuture<int> fg_AssignedEntry()
{
	int Result;
	Result = co_await fg_FirstEntry();
	co_return Result;
}

// Before an entry switches, it runs on the actor of its caller: what it
// dispatches to the current actor, and what it calls, runs there.
DMibRunsToCompletionEntry TCFuture<int> fg_DispatchBeforeSwitch()
{
	int Value = 1;
	TCFutureVector<int> Results;
	auto JoinResults = co_await fg_AsyncDestroy
		(
			[&]() -> TCFuture<int>
			{
				co_await fg_AllDone(Results);
				co_return 0;
			}
		)
	;
	g_Dispatch / [&Value]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:17: warning: a TCFuture coroutine lambda captures 'Value' by reference but is not called directly where 'Value' is, {{.*}}
		{
			co_return co_await fg_Read(Value);
// CHECK-MESSAGES: :[[@LINE-1]]:23: warning: 'fg_Read' runs to completion, but is called here where the code doesn't, {{.*}}
		}
		> Results
	;
	fg_Read(Value) > Results;
// CHECK-MESSAGES: :[[@LINE-1]]:2: warning: 'fg_Read' runs to completion, but is called here before the entry switches to a concurrent actor, {{.*}}
	co_await fg_ContinueRunningOnActor(fg_ConcurrentActor());
	g_Dispatch / [&Value]() -> TCFuture<int>
		{
			co_return co_await fg_Read(Value);
		}
		> Results
	;
	co_await fg_Move(JoinResults);
	co_return 0;
}

// fg_ConcurrentDispatch dispatches to a concurrent actor.
DMibRunsToCompletion TCFuture<int> fg_ConcurrentDispatched()
{
	int Value = 1;
	co_return co_await fg_ConcurrentDispatch
		(
			[&Value]() -> TCFuture<int>
			{
				co_return co_await fg_Read(Value);
			}
		)
	;
}

// A cleanup that returns what it awaits joins too.
DMibRunsToCompletion TCFuture<int> fg_ReturnedJoin()
{
	int Value = 1;
	TCFutureVector<int> Results;
	auto JoinResults = co_await fg_AsyncDestroy
		(
			[&]() -> TCFuture<int>
			{
				co_return co_await fg_AllDone(Results);
			}
		)
	;
	fg_Read(Value) > Results;
	co_await fg_Move(JoinResults);
	co_return 0;
}

// A copy of a lambda in an init capture runs where the copy is called.
DMibRunsToCompletion TCFuture<int> fg_CopiedLambda(TCActor<> _Other)
{
	auto fRead = [](int const &_Value) -> TCFuture<int>
		{
			co_return co_await fg_Read(_Value);
// CHECK-MESSAGES: :[[@LINE-1]]:23: warning: 'fg_Read' runs to completion, but is called here where the code doesn't, {{.*}}
		}
	;
	int Local = 42;
	co_await fRead(Local);
	co_await
		(
			g_Dispatch(_Other) / [fCopy = fRead]() -> TCFuture<int>
			{
				int Inner = 1;
				co_return co_await fCopy(Inner);
			}
		)
	;
	co_return 0;
}

// A jump back can start what is written before an early join after it.
DMibRunsToCompletion TCFuture<int> fg_JumpBack()
{
	int Value = 42;
	TCFutureVector<int> Results;
	auto JoinResults = co_await fg_AsyncDestroy
		(
			[&]() -> TCFuture<int>
			{
				co_await fg_AllDone(Results);
				co_return 0;
			}
		)
	;
	goto FinishJoin;
StartWork:
	fg_Read(Value) > Results;
// CHECK-MESSAGES: :[[@LINE-1]]:2: warning: 'fg_Read' runs to completion, but isn't joined here, {{.*}}
	co_return 0;
FinishJoin:
	co_await fg_Move(JoinResults);
	goto StartWork;
}

DMibRunsToCompletion TCFuture<int> fg_ReadPointer(int const *_pValue);

// A local array has its elements, and a structured binding is of the object
// it decomposes.
DMibRunsToCompletion TCFuture<int> fg_ArrayAndBinding()
{
	int Values[2] = {1, 2};
	struct CPair
	{
		int m_X;
		int m_Y;
	} Pair{1, 2};
	auto &[X, Y] = Pair;
	TCFutureVector<int> Results;
	auto JoinResults = co_await fg_AsyncDestroy
		(
			[&]() -> TCFuture<int>
			{
				co_await fg_AllDone(Results);
				co_return 0;
			}
		)
	;
	fg_ReadPointer(Values) > Results;
	g_Dispatch / [&X]() -> TCFuture<int>
		{
			co_await fg_Work(0);
			co_return X;
		}
		> Results
	;
	co_await fg_Move(JoinResults);
	co_return 0;
}

// A static guard lives past the call.
DMibRunsToCompletion TCFuture<int> fg_StaticGuard()
{
	int Value = 1;
	TCFutureVector<int> Results;
	static auto JoinResults = co_await fg_AsyncDestroy
		(
			[&]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:5: warning: a TCFuture coroutine lambda captures 'Results' by reference {{.*}}
			{
				co_await fg_AllDone(Results);
				co_return 0;
			}
		)
	;
	fg_Read(Value) > Results;
// CHECK-MESSAGES: :[[@LINE-1]]:2: warning: 'fg_Read' runs to completion, but isn't joined here, {{.*}}
	co_return 0;
}

// A parameter by value of a coroutine around the lambda that has the guard,
// a local reference, and either value of a conditional.
DMibRunsToCompletion TCFuture<int> fg_EnclosingParameter(int _Value, bool _bFirst)
{
	co_await fg_CallSafe
		(
			[&_Value, _bFirst]() -> TCFuture<int>
			{
				int Other = 2;
				TCFutureVector<int> Results;
				auto JoinResults = co_await fg_AsyncDestroy
					(
						[&]() -> TCFuture<int>
						{
							co_await fg_AllDone(Results);
							co_return 0;
						}
					)
				;
				fg_Read(_Value) > Results;
				{
					int &Alias = Other;
					g_Dispatch / [&Alias]() -> TCFuture<int>
						{
							co_await fg_Work(0);
							co_return Alias;
						}
						> Results
					;
				}
				fg_Read(_bFirst ? _Value : Other) > Results;
				co_await fg_Move(JoinResults);
				co_return 0;
			}
		)
	;
	co_return 0;
}

// The branch of `if constexpr` that is kept.
template <bool t_bLowPrio>
DMibRunsToCompletionEntry TCFuture<int> fg_ConstexprEntry()
{
	if constexpr (t_bLowPrio)
		co_await fg_ContinueRunningOnActor(fg_ConcurrentActorLowPrio());
	else
		co_await fg_ContinueRunningOnActor(fg_ConcurrentActor());
	co_return 0;
}

void fg_InstantiateConstexprEntry()
{
	fg_ConstexprEntry<true>();
	fg_ConstexprEntry<false>();
}

// An entry that isn't a coroutine doesn't switch.
DMibRunsToCompletionEntry TCFuture<int> fg_ForwardingEntry(TCFunction<TCFuture<int> ()> _fCallback)
// CHECK-MESSAGES: :[[@LINE-1]]:41: warning: 'fg_ForwardingEntry' begins to run to completion, but isn't a coroutine, {{.*}}
{
	return _fCallback();
}

// A switch of its own after awaiting another entry.
DMibRunsToCompletionEntry TCFuture<int> fg_Reenter()
{
	co_await fg_FirstEntry();
	co_await fg_ContinueRunningOnActor(fg_ConcurrentActor());
	int Value = 1;
	co_return co_await fg_Read(Value);
}

// Members of the object of a function that runs to completion, also as a
// value of a conditional.
struct DMibRunsToCompletion CMemberCapture
{
	TCFuture<int> f_Run(bool _bMember)
	{
		co_await fg_CallSafe
			(
				[&Value = m_Value]() -> TCFuture<int>
				{
					co_await fg_Work(0);
					co_return Value;
				}
			)
		;
		int Local = 2;
		TCFutureVector<int> Results;
		auto JoinResults = co_await fg_AsyncDestroy
			(
				[&]() -> TCFuture<int>
				{
					co_await fg_AllDone(Results);
					co_return 0;
				}
			)
		;
		fg_Read(_bMember ? m_Value : Local) > Results;
		co_await fg_Move(JoinResults);
		co_return 0;
	}

	int m_Value = 1;
};

#define DEarlyLocal int Early = 1

// A conditional whose value declared last dies before the join, also when
// the other comes from a macro.
DMibRunsToCompletion TCFuture<int> fg_MacroConditional(bool _bEarly)
{
	DEarlyLocal;
	TCFutureVector<int> Results;
	auto JoinResults = co_await fg_AsyncDestroy
		(
			[&]() -> TCFuture<int>
			{
				co_await fg_AllDone(Results);
				co_return 0;
			}
		)
	;
	{
		int Late = 2;
		fg_Read(_bEarly ? Early : Late) > Results;
// CHECK-MESSAGES: :[[@LINE-1]]:11: warning: 'fg_Read' runs to completion and is joined by 'JoinResults', but this argument may not live until then; {{.*}}
	}
	co_await fg_Move(JoinResults);
	co_return 0;
}

// What the result keeps outlives the join.
DMibRunsToCompletion TCFuture<int const *> fg_BorrowResult(int const &_Value)
// CHECK-MESSAGES: :[[@LINE-1]]:71: warning: TCFuture coroutine 'fg_BorrowResult' uses '_Value' after a suspension, {{.*}}
{
	co_return &_Value;
}

struct DMibRunsToCompletion CBorrowThis
{
	TCFuture<CBorrowThis const *> f_Self()
// CHECK-MESSAGES: :[[@LINE-1]]:32: warning: TCFuture coroutine 'f_Self' is a member of 'CBorrowThis', {{.*}}
	{
		co_return this;
	}
};

struct CRound32StrPtr
{
	char const *m_pData;
};

DMibRunsToCompletion TCFuture<int> fg_ReadView(CRound32StrPtr const &_View);

// A view by reference, a conditional of pointers, and an element of an array.
DMibRunsToCompletion TCFuture<int> fg_ViewByReference(bool _bFirst)
{
	CRound32StrPtr View{nullptr};
	int First = 1;
	int Second = 2;
	int Values[2] = {1, 2};
	TCFutureVector<int> Results;
	auto JoinResults = co_await fg_AsyncDestroy
		(
			[&]() -> TCFuture<int>
			{
				co_await fg_AllDone(Results);
				co_return 0;
			}
		)
	;
	{
		char Late = 'x';
		View.m_pData = &Late;
		fg_ReadView(View) > Results;
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'fg_ReadView' runs to completion and is joined by 'JoinResults', but this argument may not live until then; {{.*}}
	}
	fg_ReadPointer(_bFirst ? &First : &Second) > Results;
	co_await fg_CallSafe
		(
			[&Value = Values[0]]() -> TCFuture<int>
			{
				co_await fg_Work(0);
				co_return Value;
			}
		)
		.f_Wrap()
	;
	co_await fg_Move(JoinResults);
	co_return 0;
}

// A captured copy of an actor that a mutable lambda changes.
DMibRunsToCompletion TCFuture<int> fg_ChangedActor(TCActor<> _Other)
{
	auto Actor = fg_ConcurrentActor();
	co_await fg_CallSafe
		(
			[Actor, _Other]() mutable -> TCFuture<int>
			{
				Actor = _Other;
				co_await fg_ContinueRunningOnActor(Actor);
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: code that runs to completion switches here to an actor that may be destroyed {{.*}}
				co_return 0;
			}
		)
	;
	co_return 0;
}

// A borrowed address returned through a local, and by a joined lambda.
DMibRunsToCompletion TCFuture<int *> fg_ReturnedAlias(int &_Value)
// CHECK-MESSAGES: :[[@LINE-1]]:60: warning: TCFuture coroutine 'fg_ReturnedAlias' uses '_Value' after a suspension, {{.*}}
{
	int *pAlias = &_Value;
	co_await fg_Work(0);
	co_return pAlias;
}

DMibRunsToCompletion TCFuture<int *> fg_LambdaResult()
{
	int Value = 42;
	co_return co_await [&Value]() -> TCFuture<int *>
// CHECK-MESSAGES: :[[@LINE-1]]:23: warning: a TCFuture coroutine lambda uses 'Value', which it captures by reference, {{.*}}
		{
			co_return &Value;
		}
		()
	;
}

// A suspension in what the future is wrapped with can throw after the call
// started; an offset into a local array stays in it.
DMibRunsToCompletion TCFuture<int> fg_WrappedSuspension()
{
	int Local = 42;
	co_return co_await (fg_Read(Local) % (co_await fg_Work(0) ? "A" : "B"));
// CHECK-MESSAGES: :[[@LINE-1]]:22: warning: 'fg_Read' runs to completion, but isn't joined here, {{.*}}
}

DMibRunsToCompletion TCFuture<int> fg_ArrayOffset()
{
	int Values[2] = {1, 2};
	TCFutureVector<int> Results;
	auto JoinResults = co_await fg_AsyncDestroy
		(
			[&]() -> TCFuture<int>
			{
				co_await fg_AllDone(Results);
				co_return 0;
			}
		)
	;
	fg_ReadPointer(Values + 1) > Results;
	co_await fg_Move(JoinResults);
	co_return 0;
}

struct CInsertResults
{
	void f_Insert(TCFuture<int> &&_Future, int _Tag);
};
TCFuture<int> fg_AllDone(CInsertResults &_Results);

// What collects the future can leave before it does.
DMibRunsToCompletion TCFuture<int> fg_CollectLeaves()
{
	int Value = 1;
	CInsertResults Results;
	TCFutureVector<int> Vector;
	// What leaves before the early await leaves before the cleanup is joined.
	auto JoinResults = co_await fg_AsyncDestroy
		(
			[&]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:5: warning: a TCFuture coroutine lambda uses 'Vector', which it captures {{.*}}
			{
				co_await fg_AllDone(Results);
				co_await fg_AllDone(Vector);
				co_return 0;
			}
		)
	;
	Results.f_Insert(fg_Read(Value), co_await fg_Work(0));
// CHECK-MESSAGES: :[[@LINE-1]]:19: warning: 'fg_Read' runs to completion, but isn't joined here, {{.*}}
	co_await (fg_Read(Value) % (throw 1, "Context"));
	co_await fg_Move(JoinResults);
	co_return 0;
}

// A pointer assigned a borrowed address and returned.
DMibRunsToCompletion TCFuture<int *> fg_AssignedReturn(int &_Value)
// CHECK-MESSAGES: :[[@LINE-1]]:61: warning: TCFuture coroutine 'fg_AssignedReturn' uses '_Value' after a suspension, {{.*}}
{
	int *pValue = nullptr;
	co_await fg_Work(0);
	pValue = &_Value;
	co_return pValue;
}

// The object kept by the result of a joined lambda.
struct DMibRunsToCompletion CLambdaResultThis
{
	TCFuture<int> f_Run()
	{
		int *pValue = co_await [this]() -> TCFuture<int *>
// CHECK-MESSAGES: :[[@LINE-1]]:26: warning: a TCFuture coroutine lambda that 'f_Run', which runs to completion, starts and joins returns a pointer or reference into the object of 'CLambdaResultThis', {{.*}}
			{
				co_await fg_Work(0);
				co_return &m_Value;
			}
			()
		;
		co_return *pValue;
	}

	// Also through a capture of a pointer to it.
	TCFuture<int> f_RunAlias()
	{
		int *pValue = co_await [pThis = this]() -> TCFuture<int *>
// CHECK-MESSAGES: :[[@LINE-1]]:26: warning: a TCFuture coroutine lambda that 'f_RunAlias', which runs to completion, starts and joins returns a pointer or reference into the object of 'CLambdaResultThis', {{.*}}
			{
				co_await fg_Work(0);
				co_return &pThis->m_Value;
			}
			()
		;
		co_return *pValue;
	}

	int m_Value = 0;
};

namespace std
{
	template <typename t_CType>
	struct span
	{
		span(t_CType *_pData, int _Size);
		t_CType *m_pData;
		int m_Size;
	};
}

DMibRunsToCompletion TCFuture<int> fg_ReadSpan(std::span<char> _View);

// A span of a local array, and a lambda that calls itself.
DMibRunsToCompletion TCFuture<int> fg_SpanAndRecursion()
{
	char Text[8] = {};
	std::span<char> View(Text, 8);
	TCFutureVector<int> Results;
	auto JoinResults = co_await fg_AsyncDestroy
		(
			[&]() -> TCFuture<int>
			{
				co_await fg_AllDone(Results);
				co_return 0;
			}
		)
	;
	TCFunction<TCFuture<int> (int)> fRecurse = [&](int _Depth) -> TCFuture<int>
		{
			if (_Depth)
				co_return co_await fRecurse(_Depth - 1);
			co_return co_await fg_Read(_Depth);
		}
	;
	fg_ReadSpan(View) > Results;
	co_await fg_Move(JoinResults);
	co_return co_await fg_CallSafe(fRecurse, 1);
}

char const *fg_Context();

// A cleanup made by a factory uses a local only before it first suspends,
// while the local is there; after it, the local is gone.
DMibRunsToCompletion TCFuture<int> fg_FactoryLocalHooks()
{
	CState BaseDir;
	auto fMakeHook = [&]()
		{
			return [&]() -> TCFuture<int>
				{
					CState Copy = BaseDir;
					co_await fg_Work(Copy.m_Value);
					co_return 0;
				}
			;
		}
	;
	auto fLateHook = [&]()
		{
			return [&]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:12: warning: a TCFuture coroutine lambda uses 'BaseDir', which it captures by reference, after a suspension, {{.*}}
				{
					co_await fg_Work(0);
					co_return BaseDir.m_Value;
				}
			;
		}
	;
	{
		auto Restore = co_await fg_AsyncDestroy(fMakeHook());
		auto LateRestore = co_await fg_AsyncDestroy(fLateHook());
	}
	co_return 0;
}

// An early await joins the locals only when it runs on every path before
// they are gone: not in a branch, nor after something that can leave first.
DMibRunsToCompletion TCFuture<int> fg_ConditionalEarlyAwait(bool _bEarly)
{
	TCFutureVector<int> Results;
	int Local = 1;
	auto JoinResults = co_await fg_AsyncDestroy
		(
			[&]() -> TCFuture<int>
			{
				co_await fg_AllDone(Results);
				co_return 0;
			}
		)
	;
	fg_Read(Local) > Results;
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: 'fg_Read' runs to completion and is joined by 'JoinResults', but this argument may not live until then; {{.*}}
	if (_bEarly)
		co_await fg_Move(JoinResults);
	co_return 0;
}

DMibRunsToCompletion TCFuture<int> fg_LeavesBeforeEarlyAwait(bool _bEarly)
{
	TCFutureVector<int> Results;
	int Local = 1;
	auto JoinResults = co_await fg_AsyncDestroy
		(
			[&]() -> TCFuture<int>
			{
				co_await fg_AllDone(Results);
				co_return 0;
			}
		)
	;
	fg_Read(Local) > Results;
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: 'fg_Read' runs to completion and is joined by 'JoinResults', but this argument may not live until then; {{.*}}
	if (_bEarly)
		co_return 1;
	co_await fg_Move(JoinResults);
	co_return 0;
}

DMibRunsToCompletion TCFuture<int> fg_ThrowsBeforeEarlyAwait()
{
	TCFutureVector<int> Results;
	int Local = 1;
	auto JoinResults = co_await fg_AsyncDestroy
		(
			[&]() -> TCFuture<int>
			{
				co_await fg_AllDone(Results);
				co_return 0;
			}
		)
	;
	fg_Read(Local) > Results;
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: 'fg_Read' runs to completion and is joined by 'JoinResults', but this argument may not live until then; {{.*}}
	co_await fg_Work(0);
	co_await fg_Move(JoinResults);
	co_return 0;
}

DMibRunsToCompletion TCFuture<int> fg_StartsMoreBeforeEarlyAwait()
{
	TCFutureVector<int> Results;
	int Local = 1;
	auto JoinResults = co_await fg_AsyncDestroy
		(
			[&]() -> TCFuture<int>
			{
				co_await fg_AllDone(Results);
				co_return 0;
			}
		)
	;
	fg_Read(Local) > Results;
	{
		int Other = 2;
		fg_Read(Local) > Results;
		(void)Other;
	}
	co_await fg_Work(0).f_Wrap();
	co_await fg_Move(JoinResults);
	co_return 0;
}

// What has static storage lives as long as the program.
int const gc_Static = 1;
DMibRunsToCompletion TCFuture<int> fg_StaticStorage()
{
	static int const c_Value = 42;
	TCFutureVector<int> Results;
	auto JoinResults = co_await fg_AsyncDestroy
		(
			[&]() -> TCFuture<int>
			{
				co_await fg_AllDone(Results);
				co_return 0;
			}
		)
	;
	fg_Read(c_Value) > Results;
	fg_Read(gc_Static) > Results;
	co_return 0;
}

// What wraps the future must not throw: a constructor can.
struct CThrowingContext
{
	CThrowingContext();
};
// What the result keeps, also through another local.
DMibRunsToCompletion TCFuture<int *> fg_AliasedResult(int &_Value)
// CHECK-MESSAGES: :[[@LINE-1]]:60: warning: TCFuture coroutine 'fg_AliasedResult' uses '_Value' after a suspension, {{.*}}
{
	int *pFirst = &_Value;
	int *pSecond = pFirst;
	co_return pSecond;
}

// A copy of what it points to keeps nothing.
DMibRunsToCompletion TCFuture<int> fg_CopiedResult(int &_Value)
{
	int *pFirst = &_Value;
	int Copy = *pFirst;
	co_return Copy;
}

// A destructor that can throw can leave before the early await.
struct CThrowingDestructor
{
	~CThrowingDestructor() noexcept(false);
};
DMibRunsToCompletion TCFuture<int> fg_EdgeRead(int const &_Value)
{
	co_await fg_Work(0);
	co_return _Value;
}

// An early await on only some paths doesn't join before the locals go.
DMibRunsToCompletion TCFuture<int> fg_EdgeConditionalJoin(bool _bJoin)
{
	TCFutureVector<int> Results;
	int Local = 42;
	auto Guard = co_await fg_AsyncDestroy
		(
			[&]() -> TCFuture<int>
			{
				co_await fg_AllDone(Results);
				co_return 0;
			}
		)
	;
	fg_EdgeRead(Local) > Results;
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: 'fg_EdgeRead' runs to completion and is joined by 'Guard', but this argument may not live until then; pass a local declared before 'Guard' [malterlib-runs-to-completion]
	_bJoin && (co_await fg_Move(Guard), true);
	co_return 0;
}

// A default argument or a default member initializer that throws leaves
// before the future is awaited.
int fg_EdgeThrow();
struct CEdgeContext
{
};
CEdgeContext fg_EdgeContext(int _Value = fg_EdgeThrow()) noexcept;
struct CEdgeMessage
{
	int m_Value = fg_EdgeThrow();
};
// Borrowed data returned through a reference to the result.
DMibRunsToCompletion TCFuture<int const *> fg_EdgeReturnAlias(int const &_Value)
// CHECK-MESSAGES: :[[@LINE-1]]:74: warning: TCFuture coroutine 'fg_EdgeReturnAlias' uses '_Value' after a suspension, but it takes it as 'const int &', which refers to data the caller owns, which can be gone by then; use it only before the first suspension, take the data by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-parameter]
{
	int const *pResult = nullptr;
	int const *&pAlias = pResult;
	pAlias = &_Value;
	co_return pResult;
}
DMibRunsToCompletion TCFuture<int const *> fg_EdgeReturnAliasCaller()
{
	int Local = 42;
	co_return co_await fg_EdgeReturnAlias(Local);
}

// An entry that runs its callback before it switches.
template <typename tf_FCallback>
DMibRunsToCompletionEntry TCFuture<int> fg_EdgeEarlyEntry(tf_FCallback _fCallback)
{
	auto Pending = fg_CallSafe(_fCallback, 42);
	co_await fg_ContinueRunningOnActor(fg_ConcurrentActor());
	co_return co_await fg_Move(Pending);
}
TCFuture<int> fg_EdgeEarlyEntryCaller()
{
	co_return co_await fg_EdgeEarlyEntry
		(
			[](int _Value) -> TCFuture<int>
			{
				co_return co_await fg_EdgeRead(_Value);
// CHECK-MESSAGES: :[[@LINE-1]]:24: warning: 'fg_EdgeRead' runs to completion, but is called here before the entry switches to a concurrent actor, so it runs on an actor that can be destroyed while 'fg_EdgeRead' refers to its data; call it after the switch [malterlib-runs-to-completion]
			}
		)
	;
}

// One of two calls, awaited where it is made.
DMibRunsToCompletion TCFuture<int> fg_EdgeChoose(bool _bChoose, int const &_A, int const &_B)
{
	co_return co_await (_bChoose ? fg_EdgeRead(_A) : fg_EdgeRead(_B));
}

// The concurrent actor of the thread is never destroyed.
DMibRunsToCompletionEntry TCFuture<int> fg_EdgeThisConcurrent()
{
	co_await fg_ContinueRunningOnActor(fg_ThisConcurrentActor());
	co_return 0;
}

// A factory of a future that throws, and a destructor that throws where a
// block ends, both before the early await of the guard.
struct CEdgeData
{
	int *m_pValue = nullptr;
	~CEdgeData();
};
DMibRunsToCompletion TCFuture<int> fg_EdgeBorrow(CEdgeData &_Data)
{
	co_await fg_Work(0);
	co_return *_Data.m_pValue;
}
TCFuture<int> fg_EdgeThrowingFactory()
{
	throw 42;
}
struct CEdgeThrowingDestructor
{
	~CEdgeThrowingDestructor() noexcept(false);
};
// A factory that calls what throws, a collection emptied without waiting for
// it, and a lambda made before the switch but called after it.
void fg_EdgeHelperThrows();
TCFuture<int> fg_EdgeIndirectFactory()
{
	fg_EdgeHelperThrows();
	return {};
}
DMibRunsToCompletion TCFuture<int> fg_EdgeEmptied()
{
	CEdgeData Data;
	TCFutureVector<int> Results;
	auto Guard = co_await fg_AsyncDestroy
		(
			[&]() -> TCFuture<int>
			{
				co_await fg_AllDone(Results);
				co_return 0;
			}
		)
	;
	fg_EdgeBorrow(Data) > Results;
// CHECK-MESSAGES: :[[@LINE-1]]:2: warning: 'fg_EdgeBorrow' runs to completion, but isn't joined here, so it can run after what it refers to is gone; co_await it, or collect its future into a local that an fg_AsyncDestroy guard declared after the local awaits [malterlib-runs-to-completion]
	fg_AllDone(Results);
	co_await fg_Move(Guard);
	co_return 0;
}
int g_EdgeValue = 1;
DMibRunsToCompletionEntry TCFuture<int> fg_EdgeStoredLambda()
{
	auto fWork = []() -> TCFuture<int>
		{
			co_return co_await fg_Read(g_EdgeValue);
		}
	;
	co_await fg_ContinueRunningOnActor(fg_ConcurrentActor());
	co_return co_await fWork();
}

// A conditional of a local and a static, a factory whose constructor throws,
// an argument evaluated with the await, and an awaited transfer that can't be
// told.
DMibRunsToCompletion TCFuture<int> fg_EdgeChooseLocal(bool _bLocal)
{
	TCFutureVector<int> Results;
	auto Guard = co_await fg_AsyncDestroy
		(
			[&]() -> TCFuture<int>
			{
				co_await fg_AllDone(Results);
				co_return 0;
			}
		)
	;
	int Local = 42;
	static int s_Persistent = 7;
	fg_Read(_bLocal ? Local : s_Persistent) > Results;
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: 'fg_Read' runs to completion and is joined by 'Guard', but this argument may not live until then; pass a local declared before 'Guard' [malterlib-runs-to-completion]
	co_return 0;
}
struct CEdgeThrowsOnConstruction
{
	CEdgeThrowsOnConstruction();
};
TCFuture<int> fg_EdgeConstructingFactory()
{
	CEdgeThrowsOnConstruction Object;
	return {};
}
int fg_EdgeFails();
template <typename tf_CType>
void fg_EdgeConsume(tf_CType &&_Value, int _Other) noexcept;
DMibRunsToCompletion TCFuture<int> fg_EdgeGivenTransfer(CCoroutineTransferOwnership _Transfer)
{
	co_await fg_Move(_Transfer);
// CHECK-MESSAGES: :[[@LINE-1]]:2: warning: code that runs to completion switches here to an actor that can't be told, which may be destroyed while it runs and abort it; switch with fg_ContinueRunningOnActor to a concurrent actor [malterlib-runs-to-completion]
	co_return 0;
}

// A collector that throws, a factory that throws again from its handler, and
// futures joined by a pack.
void fg_EdgeConsumeFuture(TCFuture<int> _Future)
{
	throw 42;
}
TCFuture<int> fg_EdgeRethrowingFactory()
{
	try
	{
		throw 42;
	}
	catch (...)
	{
		throw;
	}
}
struct CEdgePack
{
	CThrowingAwaiter operator co_await () && noexcept;
};
CEdgePack operator + (TCFuture<int> &&_Left, TCFuture<int> &&_Right) noexcept;
DMibRunsToCompletion TCFuture<int> fg_EdgePack(int const &_A, int const &_B)
{
	co_await (fg_Read(_A) + fg_Read(_B));
	co_return 0;
}

// A throwing factory in a pack, a constructor of something else than a string
// from a literal, a function given a future that can throw, a parameter
// pointed back at what it borrows, and a pack with a bound actor call.
TCFuture<int> fg_EdgePackFactory()
{
	throw 1;
}
struct CEdgeResource
{
	explicit CEdgeResource(char const *_pName);
};
void fg_EdgeTakeFuture(TCFuture<int> _Future);
DMibRunsToCompletion TCFuture<int *> fg_EdgePointedBack(int *_pValue)
// CHECK-MESSAGES: :[[@LINE-1]]:62: warning: TCFuture coroutine 'fg_EdgePointedBack' uses '_pValue' after a suspension, but it takes it as 'int *', which refers to data the caller owns, which can be gone by then; use it only before the first suspension, take the data by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-parameter]
{
	int *pAlias = _pValue;
	_pValue = pAlias;
	co_await fg_Work(0);
	co_return _pValue;
}
struct TCBoundActorCall
{
};
CEdgePack operator + (TCFuture<int> &&_Left, TCBoundActorCall &&_Right) noexcept;
TCBoundActorCall fg_EdgeBind() noexcept;
DMibRunsToCompletion TCFuture<int> fg_EdgeBoundPack(int const &_Value)
{
	co_await (fg_Read(_Value) + fg_EdgeBind());
	co_return 0;
}

// A throw before an early join ends the program, unless the coroutine
// captures exceptions, which then leave it: such a coroutine can't give a
// local to a call that the early join joins.
void fg_EdgeMayThrow();
DMibRunsToCompletion TCFuture<int> fg_EdgeFlagsBeforeJoin()
{
	CEdgeData Data;
	TCFutureVector<int> Results;
	auto Guard = co_await fg_AsyncDestroy
		(
			[&]() -> TCFuture<int>
			{
				co_await fg_AllDone(Results);
				co_return 0;
			}
		)
	;
	fg_EdgeBorrow(Data) > Results;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'fg_EdgeBorrow' runs to completion and is joined by 'Guard', but this argument may not live until then; pass a local declared before 'Guard' [malterlib-runs-to-completion]
	co_await ECoroutineFlag_CaptureExceptions;
	try
	{
		fg_EdgeMayThrow();
	}
	catch (...)
	{
	}
	co_await fg_Move(Guard);
	co_return 0;
}
// A checkout of a blocking actor that the coroutine owns.
DMibRunsToCompletionEntry TCFuture<int> fg_EdgeOwnedCheckout(CBlockingActorCheckout _Checkout)
{
	co_await fg_ContinueRunningOnActor(_Checkout);
	co_return 0;
}

// Nor one that captures them in a scope, `co_await g_CaptureExceptions`.
void operator + (TCFuture<int> &&_Future, int _Value);
TCFuture<int> fg_EdgeOther();
DMibRunsToCompletion TCFuture<int> fg_EdgeCaptureBeforeJoin()
{
	CEdgeData Data;
	TCFutureVector<int> Results;
	auto Guard = co_await fg_AsyncDestroy
		(
			[&]() -> TCFuture<int>
			{
				co_await fg_AllDone(Results);
				co_return 0;
			}
		)
	;
	fg_EdgeBorrow(Data) > Results;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'fg_EdgeBorrow' runs to completion and is joined by 'Guard', but this argument may not live until then; pass a local declared before 'Guard' [malterlib-runs-to-completion]
	auto CaptureScope = co_await g_CaptureExceptions;
	co_await fg_Move(Guard);
	co_return 0;
}
// What an await that doesn't suspend throws is caught by a catch of
// everything.
struct CThrowOnResume : CSuspendNever
{
	void await_resume() const;
};
DMibRunsToCompletion TCFuture<int> fg_EdgeCaughtAwaitBeforeJoin()
{
	CEdgeData Data;
	TCFutureVector<int> Results;
	auto Guard = co_await fg_AsyncDestroy
		(
			[&]() -> TCFuture<int>
			{
				co_await fg_AllDone(Results);
				co_return 0;
			}
		)
	;
	fg_EdgeBorrow(Data) > Results;
	try
	{
		co_await CThrowOnResume{};
	}
	catch (...)
	{
	}
	co_await fg_Move(Guard);
	co_return 0;
}

// A pointer captured by reference that lives until the join, pointed at a
// local that doesn't.
DMibRunsToCompletion TCFuture<int> fg_EdgePointeeBeforeJoin()
{
	TCFutureVector<int> Results;
	int *pValue = nullptr;
	auto Join = co_await fg_AsyncDestroy
		(
			[&]() -> TCFuture<int>
			{
				co_await fg_AllDone(Results);
				co_return 0;
			}
		)
	;
	{
		int Inner = 7;
		pValue = &Inner;
		g_Dispatch / [&pValue]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:18: warning: a TCFuture coroutine lambda captures 'pValue' by reference but is not called directly where 'pValue' is, {{.*}}
			{
				co_await fg_Work(0);
				co_return *pValue;
			}
		> Results;
	}
	co_await fg_Move(Join);
	co_return 0;
}

// Nor a call in a `try`, whose handler catches what is thrown after it,
// which leaves the block of the call.
int fg_EdgeCaptureThrows();
DMibRunsToCompletion TCFuture<int> fg_EdgeCallInCaughtTry()
{
	CEdgeData Data;
	TCFutureVector<int> Results;
	auto Guard = co_await fg_AsyncDestroy
		(
			[&]() -> TCFuture<int>
			{
				co_await fg_AllDone(Results);
				co_return 0;
			}
		)
	;
	try
	{
		fg_EdgeBorrow(Data) > Results;
// CHECK-MESSAGES: :[[@LINE-1]]:17: warning: 'fg_EdgeBorrow' runs to completion and is joined by 'Guard', but this argument may not live until then; pass a local declared before 'Guard' [malterlib-runs-to-completion]
		fg_EdgeMayThrow();
	}
	catch (...)
	{
	}
	co_await fg_Move(Guard);
	co_return 0;
}

// Also through a copy of a pointer to it.
DMibRunsToCompletion TCFuture<int> fg_EdgePointeeCopyBeforeJoin()
{
	TCFutureVector<int> Results;
	int *pValue = nullptr;
	auto Join = co_await fg_AsyncDestroy
		(
			[&]() -> TCFuture<int>
			{
				co_await fg_AllDone(Results);
				co_return 0;
			}
		)
	;
	{
		int Inner = 7;
		int *pInner = &Inner;
		pValue = pInner;
		g_Dispatch / [&pValue]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:18: warning: a TCFuture coroutine lambda captures 'pValue' by reference but is not called directly where 'pValue' is, {{.*}}
			{
				co_await fg_Work(0);
				co_return *pValue;
			}
		> Results;
	}
	co_await fg_Move(Join);
	co_return 0;
}

// Also a local array that decays to the pointer.
DMibRunsToCompletion TCFuture<int> fg_EdgePointeeArrayBeforeJoin()
{
	TCFutureVector<int> Results;
	int *pValue = nullptr;
	auto Join = co_await fg_AsyncDestroy
		(
			[&]() -> TCFuture<int>
			{
				co_await fg_AllDone(Results);
				co_return 0;
			}
		)
	;
	{
		int Values[1] = {42};
		pValue = Values;
		g_Dispatch / [&pValue]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:18: warning: a TCFuture coroutine lambda captures 'pValue' by reference but is not called directly where 'pValue' is, {{.*}}
			{
				co_await fg_Work(0);
				co_return *pValue;
			}
		> Results;
	}
	co_await fg_Move(Join);
	co_return 0;
}

// An early await in a try, which a handler skips.
DMibRunsToCompletion TCFuture<int> fg_EdgeJoinInTry(bool _bFail)
{
	CEdgeData Data;
	TCFutureVector<int> Results;
	auto Guard = co_await fg_AsyncDestroy
		(
			[&]() -> TCFuture<int>
			{
				co_await fg_AllDone(Results);
				co_return 0;
			}
		)
	;
	fg_EdgeBorrow(Data) > Results;
// CHECK-MESSAGES: :[[@LINE-1]]:16: warning: 'fg_EdgeBorrow' runs to completion and is joined by 'Guard', but this argument may not live until then; pass a local declared before 'Guard' [malterlib-runs-to-completion]
	try
	{
		if (_bFail)
			throw 1;
		co_await fg_Move(Guard);
	}
	catch (...)
	{
	}
	co_return 0;
}

// A lambda held in a local and given to fg_CallSafe after the entry switches.
DMibRunsToCompletion TCFuture<int> fg_EdgeHeldRead()
{
	co_return 1;
}
DMibRunsToCompletionEntry TCFuture<int> fg_EdgeHeldCallSafe()
{
	auto fRead = []() -> TCFuture<int>
		{
			co_return co_await fg_EdgeHeldRead();
		}
	;
	co_await fg_ContinueRunningOnActor(fg_ConcurrentActor());
	co_return co_await fg_CallSafe(fRead);
}

// A pack of three futures, made by operators of the framework that aren't
// noexcept.
namespace NMib::NConcurrency
{
	struct TCFutureEdgePack
	{
		CThrowingAwaiter operator co_await () &&;
	};
	TCFutureEdgePack operator + (TCFuture<short> &&_Left, TCFuture<short> &&_Right);
	TCFutureEdgePack operator + (TCFutureEdgePack &&_Left, TCFuture<short> &&_Right);
}
DMibRunsToCompletion TCFuture<short> fg_EdgeReadShort(int const &_Value)
{
	co_await fg_Work(0);
	co_return short(_Value);
}
DMibRunsToCompletion TCFuture<int> fg_EdgeThreePack(int const &_A, int const &_B, int const &_C)
{
	co_await (fg_EdgeReadShort(_A) + fg_EdgeReadShort(_B) + fg_EdgeReadShort(_C));
	co_return 0;
}

// A lambda whose capture can throw in a pack, and a pointer further into a
// local array.
template <typename tf_FCallback>
TCFuture<int> fg_EdgeRunCallback(tf_FCallback _fCallback)
{
	co_return _fCallback();
}
DMibRunsToCompletion TCFuture<int> fg_EdgePointeeArithmeticBeforeJoin()
{
	TCFutureVector<int> Results;
	int *pValue = nullptr;
	auto Join = co_await fg_AsyncDestroy
		(
			[&]() -> TCFuture<int>
			{
				co_await fg_AllDone(Results);
				co_return 0;
			}
		)
	;
	{
		int Values[2] = {1, 2};
		pValue = Values + 1;
		g_Dispatch / [&pValue]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:18: warning: a TCFuture coroutine lambda captures 'pValue' by reference but is not called directly where 'pValue' is, {{.*}}
			{
				co_await fg_Work(0);
				co_return *pValue;
			}
		> Results;
	}
	co_await fg_Move(Join);
	co_return 0;
}

// A callback of fg_OnResume can abort the coroutine where it resumes, also
// through an await that can't fail by itself.
DMibRunsToCompletion TCFuture<int> fg_R51Read(int &_Value);
DMibRunsToCompletion TCFuture<int> fg_R51AbortedOnResume()
{
	int Local = 42;
	TCFutureVector<int> Results;
	auto Guard = co_await fg_AsyncDestroy
		(
			[&]() -> TCFuture<int>
			{
				co_await fg_AllDone(Results).f_Wrap();
				co_return 0;
			}
		)
	;
	auto OnResume = co_await fg_OnResume
		(
			[]() -> CExceptionPointer
			{
				return fg_MakeError("Cancelled");
			}
		)
	;
	fg_R51Read(Local) > Results;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'fg_R51Read' runs to completion and is joined by 'Guard', but this argument may not live until then; pass a local declared before 'Guard' [malterlib-runs-to-completion]
	co_await fg_Work(0).f_Wrap();
	co_await fg_Move(Guard);
	co_return 0;
}

// What gives the object of a member function in a pack can throw after the
// other future started.
struct CR51Factory
{
	DMibRunsToCompletion TCFuture<int> f_Run();
};
CR51Factory fg_R51GetFactory();
// What a view kept in a member, or a pointer that isn't local, refers to can
// be a local.
struct CStrSpan
{
	char const *m_pData;
	int m_Length;
};
DMibRunsToCompletion TCFuture<int> fg_R51ReadSpan(CStrSpan _Span);
DMibRunsToCompletion TCFuture<int> fg_R51ReadPointer(int *_pValue);
int *g_pR51Pointer;
struct DMibRunsToCompletion CR51Runner
{
	CStrSpan m_Span;
	TCFuture<int> f_Run()
	{
		TCFutureVector<int> Results;
		auto Guard = co_await fg_AsyncDestroy
			(
				[&]() -> TCFuture<int>
				{
					co_await fg_AllDone(Results);
					co_return 0;
				}
			)
		;
		{
			char Local[] = "hello";
			m_Span = {Local, 5};
			fg_R51ReadSpan(m_Span) > Results;
// CHECK-MESSAGES: :[[@LINE-1]]:19: warning: 'fg_R51ReadSpan' runs to completion and is joined by 'Guard', but this argument may not live until then; pass a local declared before 'Guard' [malterlib-runs-to-completion]
			int Value = 42;
			g_pR51Pointer = &Value;
			fg_R51ReadPointer(g_pR51Pointer) > Results;
// CHECK-MESSAGES: :[[@LINE-1]]:22: warning: 'fg_R51ReadPointer' runs to completion and is joined by 'Guard', but this argument may not live until then; pass a local declared before 'Guard' [malterlib-runs-to-completion]
		}
		co_return 0;
	}
};

// A view captured by reference refers to what it is made of.
DMibRunsToCompletion TCFuture<int> fg_R51CapturedView()
{
	CStrSpan Span{};
	TCFutureVector<int> Results;
	auto Guard = co_await fg_AsyncDestroy
		(
			[&]() -> TCFuture<int>
			{
				co_await fg_AllDone(Results);
				co_return 0;
			}
		)
	;
	{
		char Local[] = "hello";
		Span = {Local, 5};
		fg_CallSafe
			(
				[&Span]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:7: warning: a TCFuture coroutine lambda uses 'Span', which it captures by reference, after a suspension, when 'Span' may be gone; capture by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
				{
					co_await CThrowingAwaiter{};
					co_return Span.m_pData[0];
				}
			)
			> Results
		;
	}
	co_await fg_Move(Guard);
	co_return 0;
}

// An await that resumes by throwing is caught by a catch (...), unlike one
// of a future, which aborts the coroutine instead.
DMibRunsToCompletion TCFuture<int> fg_R51CaughtAwait()
{
	int Local = 42;
	TCFutureVector<int> Results;
	auto Guard = co_await fg_AsyncDestroy
		(
			[&]() -> TCFuture<int>
			{
				co_await fg_AllDone(Results);
				co_return 0;
			}
		)
	;
	fg_R51Read(Local) > Results;
	try
	{
		co_await CThrowingAwaiter{};
	}
	catch (...)
	{
	}
	co_await fg_Move(Guard);
	co_return 0;
}

DMibRunsToCompletion TCFuture<int> fg_R51CaughtFutureAwait()
{
	int Local = 42;
	TCFutureVector<int> Results;
	auto Guard = co_await fg_AsyncDestroy
		(
			[&]() -> TCFuture<int>
			{
				co_await fg_AllDone(Results);
				co_return 0;
			}
		)
	;
	fg_R51Read(Local) > Results;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'fg_R51Read' runs to completion and is joined by 'Guard', but this argument may not live until then; pass a local declared before 'Guard' [malterlib-runs-to-completion]
	try
	{
		co_await fg_Work(0);
	}
	catch (...)
	{
	}
	co_await fg_Move(Guard);
	co_return 0;
}

// A registered callback of fg_OnResume can abort at a switch to a concurrent
// actor too, but not after its registration ended.
DMibRunsToCompletion TCFuture<int> fg_R52AbortedAtSwitch()
{
	int Local = 42;
	TCFutureVector<int> Results;
	auto Guard = co_await fg_AsyncDestroy
		(
			[&]() -> TCFuture<int>
			{
				co_await fg_AllDone(Results).f_Wrap();
				co_return 0;
			}
		)
	;
	auto OnResume = co_await fg_OnResume
		(
			[]() -> CExceptionPointer
			{
				return fg_MakeError("Cancelled");
			}
		)
	;
	fg_R51Read(Local) > Results;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'fg_R51Read' runs to completion and is joined by 'Guard', but this argument may not live until then; pass a local declared before 'Guard' [malterlib-runs-to-completion]
	co_await fg_ContinueRunningOnActor(fg_ConcurrentActorLowPrio());
	co_await fg_Move(Guard);
	co_return 0;
}

DMibRunsToCompletion TCFuture<int> fg_R52EndedRegistration()
{
	int Local = 42;
	TCFutureVector<int> Results;
	auto Guard = co_await fg_AsyncDestroy
		(
			[&]() -> TCFuture<int>
			{
				co_await fg_AllDone(Results).f_Wrap();
				co_return 0;
			}
		)
	;
	{
		auto OnResume = co_await fg_OnResume
			(
				[]() -> CExceptionPointer
				{
					return fg_MakeError("Cancelled");
				}
			)
		;
	}
	fg_R51Read(Local) > Results;
	co_await fg_Work(0).f_Wrap();
	co_await fg_Move(Guard);
	co_return 0;
}

// A factory whose local throws when it is destroyed, a string class whose
// constructor throws, and a local destroyed after the early await.
struct CR52ThrowOnDestroy
{
	~CR52ThrowOnDestroy() noexcept(false);
};
TCFuture<int> fg_R52Factory()
{
	CR52ThrowOnDestroy Object;
	return {};
}
struct CStrR52Context
{
	CStrR52Context(char const *_pText)
	{
		throw 1;
	}
};
TCFuture<int> operator % (TCFuture<int> &&_Future, CStrR52Context const &_Context);
struct CR52Cleanup
{
	CR52Cleanup() noexcept {}
	~CR52Cleanup() noexcept(false);
};
DMibRunsToCompletion TCFuture<int> fg_R52DestroyedAfter()
{
	int Local = 42;
	TCFutureVector<int> Results;
	auto Guard = co_await fg_AsyncDestroy
		(
			[&]() -> TCFuture<int>
			{
				co_await fg_AllDone(Results).f_Wrap();
				co_return 0;
			}
		)
	;
	fg_R51Read(Local) > Results;
	CR52Cleanup Cleanup;
	auto &Alias = Guard;
	co_await fg_Move(Alias);
	co_return 0;
}

// A cleanup that registers a callback of fg_OnResume before it joins can be
// aborted by it.
DMibRunsToCompletion TCFuture<int> fg_R52AbortedCleanup(int &_Value)
{
	TCFutureVector<int> Results;
	auto Guard = co_await fg_AsyncDestroy
		(
			[&Results]() -> TCFuture<int>
			{
				co_await fg_OnResume
					(
						[]() -> CExceptionPointer
						{
							return fg_MakeError("Abort");
						}
					)
				;
				co_await fg_AllDone(Results).f_Wrap();
				co_return 0;
			}
		)
	;
	fg_R51Read(_Value) > Results;
// CHECK-MESSAGES: :[[@LINE-1]]:2: warning: 'fg_R51Read' runs to completion, but isn't joined here, so it can run after what it refers to is gone; co_await it, or collect its future into a local that an fg_AsyncDestroy guard declared after the local awaits [malterlib-runs-to-completion]
	co_return 0;
}

// A future that is ready without suspending aborts the coroutine when it
// failed, and what is sequenced after the early await runs after it.
int fg_R53MayThrow();
DMibRunsToCompletion TCFuture<int> fg_R53ReadyFailure()
{
	TCFutureVector<int> Results;
	auto Guard = co_await fg_AsyncDestroy
		(
			[&]() -> TCFuture<int>
			{
				co_await fg_AllDone(Results).f_Wrap();
				co_return 0;
			}
		)
	;
	int Value = 42;
	fg_R51Read(Value) > Results;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'fg_R51Read' runs to completion and is joined by 'Guard', but this argument may not live until then; pass a local declared before 'Guard' [malterlib-runs-to-completion]
	co_await []() -> TCFuture<int>
		{
			throw 1;
			co_return 0;
		}
		()
	;
	co_await fg_Move(Guard);
	co_return 0;
}
DMibRunsToCompletion TCFuture<int> fg_R53Sequenced()
{
	TCFutureVector<int> Results;
	auto Guard = co_await fg_AsyncDestroy
		(
			[&]() -> TCFuture<int>
			{
				co_await fg_AllDone(Results).f_Wrap();
				co_return 0;
			}
		)
	;
	int Value = 42;
	fg_R51Read(Value) > Results;
	(co_await fg_Move(Guard), fg_R53MayThrow());
	co_return 0;
}

// A string class whose constructor calls what throws can fail too.
void fg_R54Fail()
{
	throw 1;
}
struct CStrR54Context
{
	CStrR54Context(char const *_pText)
	{
		fg_R54Fail();
	}
};
TCFuture<int> operator % (TCFuture<int> &&_Future, CStrR54Context const &_Context);
// An awaiter of fg_OnResume made before it is awaited registers it too, a
// temporary of the call that throws when it is destroyed leaves after the call
// started, and a join with context in a cleanup joins.
DMibRunsToCompletion TCFuture<int> fg_R54StoredResume()
{
	int Local = 0;
	TCFutureVector<int> Results;
	auto Guard = co_await fg_AsyncDestroy
		(
			[&]() -> TCFuture<int>
			{
				co_await fg_AllDone(Results).f_Wrap();
				co_return 0;
			}
		)
	;
	auto Pending = fg_OnResume
		(
			[]() -> CExceptionPointer
			{
				return fg_MakeError("Cancelled");
			}
		)
	;
	auto OnResume = co_await fg_Move(Pending);
	fg_R51Read(Local) > Results;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'fg_R51Read' runs to completion and is joined by 'Guard', but this argument may not live until then; pass a local declared before 'Guard' [malterlib-runs-to-completion]
	co_await fg_Work(0).f_Wrap();
	co_await fg_Move(Guard);
	co_return 0;
}
struct CR54Throws
{
	CR54Throws() noexcept;
	CR54Throws(CR54Throws &&) noexcept;
	~CR54Throws() noexcept(false);
};
DMibRunsToCompletion TCFuture<int> fg_R54ReadWith(int &_Value, CR54Throws _Throws);
DMibRunsToCompletion TCFuture<int> fg_R54ContextJoin()
{
	int Local = 0;
	TCFutureVector<int> Results;
	auto Guard = co_await fg_AsyncDestroy
		(
			[&]() -> TCFuture<int>
			{
				co_await (fg_AllDone(Results) % "Joining results");
				co_return 0;
			}
		)
	;
	fg_R51Read(Local) > Results;
	co_await fg_Move(Guard);
	co_return 0;
}

// Making an awaiter that doesn't suspend can throw, past an early join.
struct CRound55ThrowOnAwait
{
	std::suspend_never operator co_await ()
	{
		throw 1;
	}
};
// Nor a call in a `try` with a destructor that throws after it.
DMibRunsToCompletion TCFuture<int> fg_Round55CaughtDestructor()
{
	int Value = 0;
	TCFutureVector<int> Results;
	auto Join = co_await fg_AsyncDestroy
		(
			[&]() -> TCFuture<int>
			{
				co_await fg_AllDone(Results);
				co_return 0;
			}
		)
	;
	try
	{
		CThrowingDestructor Object;
		fg_Read(Value) > Results;
// CHECK-MESSAGES: :[[@LINE-1]]:11: warning: 'fg_Read' runs to completion and is joined by 'Join', but this argument may not live until then; pass a local declared before 'Join' [malterlib-runs-to-completion]
	}
	catch (...)
	{
	}
	co_await fg_Move(Join);
	co_return 0;
}

// Either registration of a conditional can abort the coroutine where it
// resumes.
DMibRunsToCompletion TCFuture<int> fg_Round56Conditional(bool _bChoose)
{
	int Local = 42;
	TCFutureVector<int> Results;
	auto Guard = co_await fg_AsyncDestroy
		(
			[&]() -> TCFuture<int>
			{
				co_await fg_AllDone(Results).f_Wrap();
				co_return 0;
			}
		)
	;
	auto fCancel = []() -> CExceptionPointer
		{
			return fg_MakeError("Cancelled");
		}
	;
	auto OnResume = _bChoose ? co_await fg_OnResume(fCancel) : co_await fg_OnResume(fCancel);
	fg_R51Read(Local) > Results;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'fg_R51Read' runs to completion and is joined by 'Guard', but this argument may not live until then; {{.*}}
	co_await fg_Work(0).f_Wrap();
	co_await fg_Move(Guard);
	co_return 0;
}

// Making an awaiter by an operator co_await that isn't in the file can throw,
// and so can a factory that deletes what has a throwing destructor.
struct CRound57External
{
	std::suspend_never operator co_await ();
};
TCFuture<int> fg_Round57DeletingFactory(CThrowingDestructor *_pObject)
{
	delete _pObject;
	return {};
}
// A pack of futures aborts the coroutine when one fails, also in the `try` of
// a `catch (...)`, and a discarded branch of an `if constexpr` doesn't run.
struct TCFuturePackAwaiter
{
	bool await_ready() const noexcept
	{
		return false;
	}
	void await_suspend(std::coroutine_handle<>);
	int await_resume() noexcept(false);
};
struct TCFuturePack
{
	TCFuturePackAwaiter operator co_await () &&;
};
TCFuturePack operator * (TCFuture<int> &&_Left, TCFuture<int> &&_Right);
DMibRunsToCompletion TCFuture<int> fg_Round58Pack()
{
	int Value = 7;
	TCFutureVector<int> Results;
	auto Join = co_await fg_AsyncDestroy
		(
			[&]() -> TCFuture<int>
			{
				co_await fg_AllDone(Results);
				co_return 0;
			}
		)
	;
	fg_Read(Value) > Results;
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: 'fg_Read' runs to completion and is joined by 'Join', but this argument may not live until then; {{.*}}
	try
	{
		co_await (fg_Work(0) * fg_Work(1));
	}
	catch (...)
	{
	}
	co_await fg_Move(Join);
	co_return 0;
}
DMibRunsToCompletion TCFuture<int> fg_Round58Discarded()
{
	int Value = 7;
	TCFutureVector<int> Results;
	auto Join = co_await fg_AsyncDestroy
		(
			[&]() -> TCFuture<int>
			{
				co_await fg_AllDone(Results);
				co_return 0;
			}
		)
	;
	fg_Read(Value) > Results;
	if constexpr (false)
	{
		throw 1;
	}
	co_await fg_Move(Join);
	co_return 0;
}

// What gives coroutine flags can throw, wrapping a future doesn't, a pointer
// capture of a coroutine that is joined, and what isn't evaluated in an
// entry.
ECoroutineFlag fg_Round59Flags();
DMibRunsToCompletion TCFuture<int> fg_Round59ThrowingFlags()
{
	int Value = 0;
	TCFutureVector<int> Results;
	auto Join = co_await fg_AsyncDestroy
		(
			[&]() -> TCFuture<int>
			{
				co_await fg_AllDone(Results);
				co_return 0;
			}
		)
	;
	fg_Read(Value) > Results;
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: 'fg_Read' runs to completion and is joined by 'Join', but this argument may not live until then; {{.*}}
	co_await fg_Round59Flags();
	co_await fg_Move(Join);
	co_return 0;
}
namespace NMib::NConcurrency
{
	template <>
	struct TCFuture<long>
	{
		struct CNoUnwrapAsyncResult
		{
			TCAwaiter<long> operator co_await () && noexcept
			{
				return {};
			}
		};
		CNoUnwrapAsyncResult f_Wrap() &&
		{
			return {};
		}
	};
}
TCFuture<long> fg_Round59Work();
DMibRunsToCompletion TCFuture<int> fg_Round59Wrapped()
{
	int Value = 0;
	TCFutureVector<int> Results;
	auto Join = co_await fg_AsyncDestroy
		(
			[&]() -> TCFuture<int>
			{
				co_await fg_AllDone(Results);
				co_return 0;
			}
		)
	;
	fg_Read(Value) > Results;
	co_await fg_Round59Work().f_Wrap();
	co_await fg_Move(Join);
	co_return 0;
}
DMibRunsToCompletion TCFuture<int> fg_Round59JoinedPointer()
{
	int Local = 42;
	co_return co_await fg_CallSafe
		(
			[pLocal = &Local]() -> TCFuture<int>
			{
				co_await fg_Work(0);
				co_return *pLocal;
			}
		)
	;
}
DMibRunsToCompletion TCFuture<int> fg_Round59Callback();
DMibRunsToCompletionEntry TCFuture<int> fg_Round59Entry(TCFunction<TCFuture<int> ()> _fCallback)
{
	static_assert(sizeof(_fCallback) > 0);
	co_await fg_ContinueRunningOnActor(fg_ConcurrentActor());
	co_return co_await _fCallback();
}
TCFuture<int> fg_Round59UseEntry()
{
	co_return co_await fg_Round59Entry
		(
			[]() -> TCFuture<int>
			{
				co_return co_await fg_Round59Callback();
			}
		)
	;
}

// A phase guard registers a callback with `fg_OnResume`, which can end the
// coroutine where it resumes, before the join.
DMibRunsToCompletion TCFuture<int> fg_Round60Guarded(CPhaseActor *_pThis)
{
	auto CheckDestroy = co_await _pThis->f_CheckDestroyedOnResume();
	TCFutureVector<int> Results;
	int Local = 5;
	auto JoinResults = co_await fg_AsyncDestroy
		(
			[&]() -> TCFuture<int>
			{
				co_await fg_AllDone(Results);
				co_return 0;
			}
		)
	;
	fg_Read(Local) > Results;
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: 'fg_Read' runs to completion and is joined by 'JoinResults', but this argument may not live until then; {{.*}}
	co_await fg_Work(0).f_Wrap();
	co_await fg_Move(JoinResults);
	co_return 0;
}

// A dynamic concurrent actor is concurrent, and always alive.
DMibRunsToCompletionEntry TCFuture<int> fg_Round61Dynamic()
{
	co_await fg_ContinueRunningOnActor(fg_DynamicConcurrentActorLowPrio());
	co_return 0;
}

// The locals of the init statement of an `if` live only as long as it, and
// their destructors run there, before an early await after it.
struct CRound61Throws
{
	~CRound61Throws() noexcept(false);
};
DMibRunsToCompletion TCFuture<int> fg_Round61IfLocal(bool _bRead)
{
	TCFutureVector<int> Results;
	auto JoinResults = co_await fg_AsyncDestroy
		(
			[&]() -> TCFuture<int>
			{
				co_await fg_AllDone(Results);
				co_return 0;
			}
		)
	;
	if (int Local = 42; _bRead)
		fg_Read(Local) > Results;
// CHECK-MESSAGES: :[[@LINE-1]]:11: warning: 'fg_Read' runs to completion and is joined by 'JoinResults', but this argument may not live until then; {{.*}}
	co_await fg_Move(JoinResults);
	co_return 0;
}
// An entry that awaited another entry runs on the actor it was called on,
// where it can be aborted, until it switches itself.
DMibRunsToCompletionEntry TCFuture<int> fg_Round61FirstEntry()
{
	co_await fg_ContinueRunningOnActor(fg_ConcurrentActor());
	co_return 0;
}
DMibRunsToCompletionEntry TCFuture<int> fg_Round61AfterEntry()
{
	co_await fg_Round61FirstEntry();
	int Value = 0;
	co_await fg_CallSafe
		(
			[&Value]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:6: warning: a TCFuture coroutine lambda uses 'Value', which it captures by reference, after a suspension, when 'Value' may be gone; capture by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
			{
				co_await fg_ContinueRunningOnActor(fg_ConcurrentActor());
				co_await fg_Work(0);
				co_return ++Value;
			}
		)
	;
	co_return Value;
}

// An await around the call can end the coroutine before the early join, like
// a phase guard awaited through an awaiter made before, and a concurrent
// actor moved into the switch is still that actor.
DMibRunsToCompletion TCFuture<int> fg_Round62AroundCall()
{
	TCFutureVector<int> Results;
	auto JoinResults = co_await fg_AsyncDestroy
		(
			[&]() -> TCFuture<int>
			{
				co_await fg_AllDone(Results);
				co_return 0;
			}
		)
	;
	int Local = 1;
	co_await (fg_Read(Local) > Results, fg_Work(0));
// CHECK-MESSAGES: :[[@LINE-1]]:20: warning: 'fg_Read' runs to completion and is joined by 'JoinResults', but this argument may not live until then; pass a local declared before 'JoinResults' [malterlib-runs-to-completion]
	co_await fg_Move(JoinResults);
	co_return 0;
}
DMibRunsToCompletion TCFuture<int> fg_Round62StoredGuard(CPhaseActor *_pThis)
{
	TCFutureVector<int> Results;
	auto JoinResults = co_await fg_AsyncDestroy
		(
			[&]() -> TCFuture<int>
			{
				co_await fg_AllDone(Results);
				co_return 0;
			}
		)
	;
	auto Awaiter = _pThis->f_CheckDestroyedOnResume();
	auto Registration = co_await fg_Move(Awaiter);
	int Local = 1;
	fg_Read(Local) > Results;
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: 'fg_Read' runs to completion and is joined by 'JoinResults', but this argument may not live until then; pass a local declared before 'JoinResults' [malterlib-runs-to-completion]
	co_await fg_Work(0).f_Wrap();
	co_await fg_Move(JoinResults);
	co_return 0;
}
DMibRunsToCompletionEntry TCFuture<int> fg_Round62MovedActor()
{
	auto Actor = fg_ConcurrentActor();
	co_await fg_ContinueRunningOnActor(fg_Move(Actor));
	co_return 0;
}

// A string of a literal whose constructor makes a member that throws can fail
// before the future is awaited.
struct CRound63ThrowingMember
{
	CRound63ThrowingMember()
	{
		throw 1;
	}
};
struct CRound63StrContext
{
	CRound63ThrowingMember m_Member;
	CRound63StrContext(char const *_pText) : m_Member()
	{
	}
};
// A loop around the call runs its condition again after it, a constructor
// around it runs after it, and parentheses around an early await keep it one.
bool fg_Round64Next();
struct CRound64Throws
{
	CRound64Throws(int _Value)
	{
		throw _Value;
	}
};
DMibRunsToCompletion TCFuture<int> fg_Round64Parenthesized()
{
	int Local = 42;
	TCFutureVector<int> Results;
	auto JoinResults = co_await fg_AsyncDestroy
		(
			[&]() -> TCFuture<int>
			{
				co_await fg_AllDone(Results);
				co_return 0;
			}
		)
	;
	fg_Read(Local) > Results;
	co_await (fg_Move(JoinResults).f_Wrap());
	co_return 0;
}

// Registering a callback through an awaiter made before doesn't suspend.
TCFuture<int> fg_Round65Stored(int const &_Value)
{
	auto Awaiter = fg_OnResume
		(
			[]() -> CExceptionPointer
			{
				return {};
			}
		)
	;
	auto Registration = co_await fg_Move(Awaiter);
	co_return _Value;
}

// A factory of a future around the call that throws first, the left side of
// an assignment, which runs after the right side, and the temporary a loop
// goes over, which is destroyed after the loop, can leave before the early
// join.
TCFuture<int> fg_Round65Factory(int _Value)
{
	throw _Value;
	return {};
}
int &fg_Round65Destination();
struct CRound65Range
{
	int m_Data[1]{};
	CRound65Range() noexcept
	{
	}
	~CRound65Range() noexcept(false);
	int *begin() noexcept
	{
		return m_Data;
	}
	int *end() noexcept
	{
		return m_Data + 1;
	}
};
// A default argument of what a factory calls runs in the factory; a temporary
// a local reference extends lives as long as the reference; and what is
// discarded or isn't evaluated doesn't change the guard or the collection.
struct CRound66Value
{
	int m_Value;
};
DMibRunsToCompletion TCFuture<int> fg_Round66ReadValue(CRound66Value const &_Value);
int fg_Round66Fail();
void fg_Round66Consume(int _Value = fg_Round66Fail()) noexcept;
TCFuture<int> fg_Round66Factory()
{
	fg_Round66Consume();
	return {};
}
DMibRunsToCompletion TCFuture<int> fg_Round66Extended()
{
	TCFutureVector<int> Results;
	auto JoinResults = co_await fg_AsyncDestroy
		(
			[&]() -> TCFuture<int>
			{
				co_await fg_AllDone(Results);
				co_return 0;
			}
		)
	;
	CRound66Value const &Value = CRound66Value{42};
	fg_Round66ReadValue(Value) > Results;
	co_await fg_Move(JoinResults);
	co_return 0;
}
DMibRunsToCompletion TCFuture<int> fg_Round66NeverRun(int &_Value)
{
	TCFutureVector<int> Results;
	auto JoinResults = co_await fg_AsyncDestroy
		(
			[&]() -> TCFuture<int>
			{
				co_await fg_AllDone(Results);
				co_return 0;
			}
		)
	;
	if constexpr (false)
		JoinResults.f_Clear();
	if constexpr (false)
		Results = {};
	(void)sizeof(Results);
	fg_Read(_Value) > Results;
	co_return 0;
}

// What initializes the captures of a lambda runs where it is made, and the
// left side of an assignment runs after the await on its right side.
int fg_Round67Fail();
int &fg_Round67Destination();
DMibRunsToCompletion TCFuture<int> fg_Round67Assigned()
{
	TCFutureVector<int> Results;
	auto JoinResults = co_await fg_AsyncDestroy
		(
			[&]() -> TCFuture<int>
			{
				co_await fg_AllDone(Results);
				co_return 0;
			}
		)
	;
	int Value = 0;
	fg_Read(Value) > Results;
	fg_Round67Destination() = (co_await fg_Move(JoinResults).f_Wrap(), 0);
	co_return 0;
}

// What initializes the captures of a lambda a factory makes runs in the
// factory, and can throw.
TCFuture<int> fg_Round68Factory()
{
	auto fCallback = [Value = fg_Round67Fail()]
		{
			return Value;
		}
	;
	return {};
}
// A string literal lives as long as the program, and a null pointer refers to
// nothing.
DMibRunsToCompletion TCFuture<int> fg_Round69Read(char const *_pText);
DMibRunsToCompletion TCFuture<int> fg_Round69Literal()
{
	TCFutureVector<int> Results;
	auto JoinResults = co_await fg_AsyncDestroy
		(
			[&]() -> TCFuture<int>
			{
				co_await fg_AllDone(Results);
				co_return 0;
			}
		)
	;
	fg_Round69Read(nullptr) > Results;
	fg_Round69Read("Text") > Results;
	co_return 0;
}

// A pointer to either of two locals captured by a lambda the guard joins:
// both must live until the join.
DMibRunsToCompletion TCFuture<int> fg_Round69Either(bool _bFirst)
{
	int First = 1;
	TCFutureVector<int> Results;
	auto JoinResults = co_await fg_AsyncDestroy
		(
			[&]() -> TCFuture<int>
			{
				co_await fg_AllDone(Results);
				co_return 0;
			}
		)
	;
	{
		int Second = 2;
		g_ConcurrentDispatch / [pValue = _bFirst ? &First : &Second]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:27: warning: a TCFuture coroutine lambda captures 'pValue', which points to 'Second', but is not called directly where 'Second' is, so it can run after 'Second' is gone; capture a value or a shared pointer, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
			{
				co_await fg_Work(0);
				co_return *pValue;
			}
			> Results
		;
	}
	co_await fg_Move(JoinResults);
	co_return 0;
}

// A dynamic_cast to a reference can throw.
struct CRound70Base
{
	virtual ~CRound70Base() = default;
};
struct CRound70Derived : CRound70Base
{
};
// A typeid of an object through a pointer can throw, and the elements of a
// braced list run in order.
namespace std
{
	class type_info;
}
DMibRunsToCompletion TCFuture<int> fg_Round71Ordered()
{
	TCFutureVector<int> Results;
	auto JoinResults = co_await fg_AsyncDestroy
		(
			[&]() -> TCFuture<int>
			{
				co_await fg_AllDone(Results);
				co_return 0;
			}
		)
	;
	int Value = 42;
	int Ordered[] = {fg_Round67Fail(), (fg_Read(Value) > Results, 0)};
	co_await fg_Move(JoinResults);
	co_return Ordered[0];
}

// Lambdas held in locals of lambdas, and a hierarchy of shared bases with
// overrides, are each looked at once, not once per path to them, which is
// exponential in their depth.
#define DRound72Wrap(d_Inner) \
	[]() -> TCFuture<int> \
	{ \
		auto fInner = d_Inner; \
		co_await fg_CallSafe(fInner); \
		co_return 0; \
	}
DMibRunsToCompletion TCFuture<int> fg_Round72Leaf();
DMibRunsToCompletion TCFuture<int> fg_Round72Nested()
{
	auto fOuter = DRound72Wrap(DRound72Wrap(DRound72Wrap(DRound72Wrap(DRound72Wrap(DRound72Wrap(DRound72Wrap(DRound72Wrap(
		DRound72Wrap(DRound72Wrap(DRound72Wrap(DRound72Wrap(DRound72Wrap(DRound72Wrap(DRound72Wrap(DRound72Wrap(DRound72Wrap(DRound72Wrap(DRound72Wrap(DRound72Wrap(
		[]() -> TCFuture<int>
		{
			co_return co_await fg_Round72Leaf();
		}
	))))))))))))))))))));
	co_await fg_CallSafe(fOuter);
	co_return 0;
}
template <int t_Depth>
struct TCRound72Right;
template <int t_Depth>
struct TCRound72Left : virtual TCRound72Left<t_Depth - 1>, virtual TCRound72Right<t_Depth - 1>
{
	void f_Run() override
	{
	}
};
template <int t_Depth>
struct TCRound72Right : virtual TCRound72Left<t_Depth - 1>, virtual TCRound72Right<t_Depth - 1>
{
	void f_Run() override
	{
	}
};
template <>
struct TCRound72Left<0>
{
	virtual void f_Run()
	{
	}
};
template <>
struct TCRound72Right<0>
{
	virtual void f_Run()
	{
	}
};
struct CRound72Final : TCRound72Left<18>, TCRound72Right<18>
{
	void f_Run() override
	{
	}
	TCFuture<int> f_Read(int _Value)
	{
		co_return _Value;
	}
};
template <int t_Depth, int t_Tag>
struct TCRound72Layer : virtual TCRound72Layer<t_Depth - 1, 0>, virtual TCRound72Layer<t_Depth - 1, 1>
{
};
template <int t_Tag>
struct TCRound72Layer<0, t_Tag>
{
};
struct CRound72Deep : TCRound72Layer<30, 0>
{
	static void fs_Run();
};
void fg_Round72Deep()
{
	CRound72Deep::fs_Run();
}

// A dynamic_cast to a reference in what the future is wrapped with can throw
// after the call started, and a typeid of a reference can't throw.
struct CRound72Derived : CRound70Base
{
	int m_Text;
};
DMibRunsToCompletion TCFuture<int> fg_Round72Read(int &_Value);
DMibRunsToCompletion TCFuture<int> fg_Round72Typeid(CRound70Base &_Base)
{
	TCFutureVector<int> Results;
	auto JoinResults = co_await fg_AsyncDestroy
		(
			[&]() -> TCFuture<int>
			{
				co_await fg_AllDone(Results);
				co_return 0;
			}
		)
	;
	int Value = 42;
	fg_Round72Read(Value) > Results;
	(void)typeid(_Base);
	co_await fg_Move(JoinResults);
	co_return 0;
}

// A temporary a local reference extends is destroyed with the block, where
// its destructor can throw past the early await.
struct CRound73Throws
{
	CRound73Throws() noexcept;
	~CRound73Throws() noexcept(false);
};
// What collects the future with a member function may throw, which ends the
// program.
struct CRound73Collector
{
	void f_Insert(TCFuture<int> &&_Future);
	TCFutureVector<int> m_Futures;
};
struct CRound73NoexceptCollector
{
	void f_Insert(TCFuture<int> &&_Future) noexcept;
	TCFutureVector<int> m_Futures;
};
TCFuture<int> fg_AllDone(CRound73Collector &_Results);
TCFuture<int> fg_AllDone(CRound73NoexceptCollector &_Results) noexcept;
DMibRunsToCompletion TCFuture<int> fg_Round73Collect(int const &_Value)
{
	CRound73Collector Results;
	auto Join = co_await fg_AsyncDestroy
		(
			[&]() -> TCFuture<int>
			{
				co_await fg_AllDone(Results);
				co_return 0;
			}
		)
	;
	Results.f_Insert(fg_Read(_Value));
	co_return 0;
}
DMibRunsToCompletion TCFuture<int> fg_Round73CollectNoexcept(int const &_Value)
{
	CRound73NoexceptCollector Results;
	auto Join = co_await fg_AsyncDestroy
		(
			[&]() -> TCFuture<int>
			{
				co_await fg_AllDone(Results);
				co_return 0;
			}
		)
	;
	Results.f_Insert(fg_Read(_Value));
	co_return 0;
}

// A cast of a pointer is of what it points to.
DMibRunsToCompletion TCFuture<int> fg_Round73ReadPointer(int const *_pValue);
DMibRunsToCompletion TCFuture<int> fg_Round73Cast(int &_Value)
{
	TCFutureVector<int> Results;
	auto Join = co_await fg_AsyncDestroy
		(
			[&]() -> TCFuture<int>
			{
				co_await fg_AllDone(Results);
				co_return 0;
			}
		)
	;
	fg_Round73ReadPointer(static_cast<int const *>(&_Value)) > Results;
	fg_Round73ReadPointer((int const *)&_Value) > Results;
	co_return 0;
}

// A factory that allocates with an allocation function that can throw, and
// one whose throw is in a branch an `if constexpr` discards.
struct CRound74Throwing
{
	static void *operator new(decltype(sizeof(0)) _Size)
	{
		throw 1;
	}
};
TCFuture<int> fg_Round74Allocating(TCFuture<int> _Future)
{
	auto *pObject = new CRound74Throwing;
	return _Future;
}
TCFuture<int> fg_Round74Discarded(TCFuture<int> _Future)
{
	if constexpr (false)
		throw 1;
	return _Future;
}
DMibRunsToCompletion TCFuture<int> fg_Round74DiscardedFactory()
{
	int Value = 0;
	TCFutureVector<int> Results;
	auto Join = co_await fg_AsyncDestroy
		(
			[&]() -> TCFuture<int>
			{
				co_await fg_AllDone(Results);
				co_return 0;
			}
		)
	;
	fg_Read(Value) > Results;
	fg_Round74Discarded(fg_Work(0)) > Results;
	co_await fg_Move(Join);
	co_return 0;
}

// A pointer cast or exchanged to a local that doesn't live until the join, a
// collector that throws, and a guard kept in a reference that extends it.
DMibRunsToCompletion TCFuture<int> fg_Round75Pointer()
{
	int *pValue = nullptr;
	int *pExchanged = nullptr;
	TCFutureVector<int> Results;
	auto Join = co_await fg_AsyncDestroy
		(
			[&]() -> TCFuture<int>
			{
				co_await fg_AllDone(Results);
				co_return 0;
			}
		)
	;
	{
		int Inner = 1;
		pValue = static_cast<int *>(&Inner);
		fg_Exchange(pExchanged, &Inner);
		g_Dispatch / [&pValue]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:18: warning: a TCFuture coroutine lambda captures 'pValue' by reference but is not called directly where 'pValue' is, so it can run after 'pValue' is gone; capture by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
			{
				co_await fg_Work(0);
				co_return *pValue;
			}
			> Results
		;
		g_Dispatch / [&pExchanged]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:18: warning: a TCFuture coroutine lambda captures 'pExchanged' by reference but is not called directly where 'pExchanged' is, so it can run after 'pExchanged' is gone; capture by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
			{
				co_await fg_Work(0);
				co_return *pExchanged;
			}
			> Results
		;
	}
	co_await fg_Move(Join);
	co_return 0;
}
struct CRound75Throwing
{
};
void operator>(TCFuture<int> &&_Future, CRound75Throwing &_Results)
{
	throw 1;
}
TCFuture<int> fg_AllDone(CRound75Throwing &_Results);
DMibRunsToCompletion TCFuture<int> fg_Round75ExtendedGuard(int const &_Value)
{
	TCFutureVector<int> Results;
	auto &&Join = co_await fg_AsyncDestroy
		(
			[&]() -> TCFuture<int>
			{
				co_await fg_AllDone(Results);
				co_return 0;
			}
		)
	;
	fg_Read(_Value) > Results;
	co_return 0;
}

// What isn't evaluated can't leave before the early await.
int fg_Round76MayThrow();
DMibRunsToCompletion TCFuture<int> fg_Round76Unevaluated()
{
	TCFutureVector<int> Results;
	auto Join = co_await fg_AsyncDestroy
		(
			[&]() -> TCFuture<int>
			{
				co_await fg_AllDone(Results);
				co_return 0;
			}
		)
	;
	int Value = 42;
	fg_Read(Value) > Results;
	constexpr bool bNoexcept = noexcept(fg_Round76MayThrow());
	co_await fg_Move(Join);
	co_return bNoexcept;
}

// An awaiter whose constructor throws leaves before the early await joins.
struct CRound77Throwing : std::suspend_never
{
	CRound77Throwing()
	{
		throw 1;
	}
};
// What isn't evaluated in the context of an await doesn't leave.
int fg_Round77MayThrow();
DMibRunsToCompletion TCFuture<int> fg_Round77UnevaluatedContext()
{
	int Value = 1;
	co_await (fg_Read(Value) % (sizeof(fg_Round77MayThrow()), "Context"));
	co_await (fg_Read(Value) % (noexcept(fg_Round77MayThrow()), "Context"));
	co_return 0;
}

// The future that `%` gives with an exception transformer is awaited like
// one.
namespace NMib::NConcurrency
{
	template <typename t_CType>
	struct TCFutureWithExceptionTransformer
	{
		TCFuture<t_CType> f_Wrap() &&;
		TCAwaiter<t_CType> operator co_await() &&;
	};
}
NMib::NConcurrency::TCFutureWithExceptionTransformer<int> fg_Round77Transformed();
DMibRunsToCompletion TCFuture<int> fg_Round77Transformer()
{
	TCFutureVector<int> Results;
	auto Join = co_await fg_AsyncDestroy
		(
			[&]() -> TCFuture<int>
			{
				co_await fg_AllDone(Results);
				co_return 0;
			}
		)
	;
	int Value = 1;
	fg_Read(Value) > Results;
	co_await fg_Round77Transformed().f_Wrap();
	co_await fg_Move(Join);
	co_return 0;
}

// A virtual call can run an override that throws before it gives its future.
struct CRound78Base
{
	virtual TCFuture<int> f_Work()
	{
		co_return 0;
	}
};
struct CRound78Derived : CRound78Base
{
	TCFuture<int> f_Work() override
	{
		throw 1;
	}
};
// A wrapped future kept in a local is awaited like one.
TCFuture<int> fg_Round78Work();
DMibRunsToCompletion TCFuture<int> fg_Round78StoredWrap()
{
	TCFutureVector<int> Results;
	auto Join = co_await fg_AsyncDestroy
		(
			[&]() -> TCFuture<int>
			{
				co_await fg_AllDone(Results).f_Wrap();
				co_return 0;
			}
		)
	;
	int Value = 1;
	fg_Read(Value) > Results;
	auto Future = fg_Round78Work();
	auto Wrapped = fg_Move(Future).f_Wrap();
	co_await fg_Move(Wrapped);
	co_await fg_Move(Join);
	co_return 0;
}

// A context string whose constructor makes a lambda runs what initializes its
// captures, a constructor that calls too many functions to look into can
// throw, and a branch of an `if constexpr` that is discarded doesn't run.
int fg_Round78Fail()
{
	throw 1;
}
void fg_Round78Nothing()
{
}
void fg_Round78Throw()
{
	throw 1;
}
struct CRound78CaptureStr
{
	CRound78CaptureStr(char const *_pValue)
	{
		auto fCallback = [Value = fg_Round78Fail()] {};
	}
};
struct CRound78RepeatStr
{
	CRound78RepeatStr(char const *_pValue)
	{
		fg_Round78Nothing(); fg_Round78Nothing(); fg_Round78Nothing(); fg_Round78Nothing();
		fg_Round78Nothing(); fg_Round78Nothing(); fg_Round78Nothing(); fg_Round78Nothing();
		fg_Round78Nothing(); fg_Round78Nothing(); fg_Round78Nothing(); fg_Round78Nothing();
		fg_Round78Nothing(); fg_Round78Nothing(); fg_Round78Nothing(); fg_Round78Nothing();
		fg_Round78Throw();
	}
};
struct CRound78DiscardedStr
{
	CRound78DiscardedStr(char const *_pValue)
	{
		if constexpr (false)
			fg_Round78Throw();
	}
};
// An override in a local class, deleting what has a destructor that can throw
// in the context, and a `%` that can throw, leave before the join.
struct CRound79Base
{
	virtual TCFuture<int> f_Work()
	{
		co_return 0;
	}
};
struct CRound79Throws
{
	~CRound79Throws() noexcept(false)
	{
		throw 1;
	}
};
struct CRound79Context
{
};
TCFuture<int> operator%(TCFuture<int> &&_Future, CRound79Context _Context)
{
	throw 1;
}
// A `%` that can throw in the cleanup doesn't join, and a local pointer made
// from a conditional points to either.
struct CRound80Context
{
};
TCFuture<int> operator%(TCFuture<int> &&_Future, CRound80Context _Context)
{
	throw 1;
}
DMibRunsToCompletion TCFuture<int> fg_Round80Conditional(bool _bChoose)
{
	int First = 1;
	int Second = 2;
	int *pValue = _bChoose ? &First : &Second;
	TCFutureVector<int> Results;
	auto Join = co_await fg_AsyncDestroy
		(
			[&]() -> TCFuture<int>
			{
				co_await fg_AllDone(Results);
				co_return 0;
			}
		)
	;
	fg_ReadPointer(pValue) > Results;
	co_await fg_Move(Join);
	co_return 0;
}

// What collects a future and isn't of the framework can throw, so can a `+`
// that packs one, and making the awaiter of a direct await.
TCFuture<int> fg_Round81Work();
struct CRound81OtherResults
{
	void f_Insert(TCFuture<int> &&_Future);
};
struct CRound81Packed
{
};
TCFuture<int> operator+(TCFuture<int> &&_Left, CRound81Packed _Right)
{
	throw 1;
}
DMibRunsToCompletion TCFuture<int> fg_Round81Pack()
{
	int Value = 42;
	co_return co_await (fg_Read(Value) + CRound81Packed{});
// CHECK-MESSAGES: :[[@LINE-1]]:22: warning: 'fg_Read' runs to completion, but isn't joined here, so it can run after what it refers to is gone; co_await it, or collect its future into a local that an fg_AsyncDestroy guard declared after the local awaits [malterlib-runs-to-completion]
}
struct CRound81Task
{
	struct promise_type
	{
		CRound81Task get_return_object();
		std::suspend_never initial_suspend();
		std::suspend_never final_suspend() noexcept;
		void return_void();
		void unhandled_exception();
		TCAwaiter<int> await_transform(TCFuture<int> &&_Future)
		{
			throw 1;
		}
	};
};
struct CRound81Tag
{
};
struct CRound81ThrowingPack
{
	CThrowingAwaiter operator co_await () &&;
};
CRound81ThrowingPack operator + (TCFuture<int> &&_Left, CRound81Tag _Right) noexcept;
DMibRunsToCompletion TCFuture<int> fg_Round81ThrowingPack()
{
	int Value = 42;
	co_await (fg_Read(Value) + CRound81Tag{});
// CHECK-MESSAGES: :[[@LINE-1]]:12: warning: 'fg_Read' runs to completion, but isn't joined here, so it can run after what it refers to is gone; co_await it, or collect its future into a local that an fg_AsyncDestroy guard declared after the local awaits [malterlib-runs-to-completion]
	co_return 0;
}

// Nested awaits in an entry are looked at once each, not once per path
// through the operands, which is exponential in their depth.
#define DRound82Await(d_Value) co_await fg_Work(d_Value)
#define DRound82Await4(d_Value) DRound82Await(DRound82Await(DRound82Await(DRound82Await(d_Value))))
#define DRound82Await32(d_Value) DRound82Await4(DRound82Await4(DRound82Await4(DRound82Await4(DRound82Await4(DRound82Await4(DRound82Await4(DRound82Await4(d_Value))))))))
DMibRunsToCompletionEntry TCFuture<int> fg_Round82Entry()
{
	co_await fg_ContinueRunningOnActor(fg_ConcurrentActor());
	co_return DRound82Await32(0);
}

// A string made of a literal whose constructor throws through a default
// argument or a default member initializer, and an early join after a branch
// of an `if constexpr` that is discarded.
void fg_Round83Helper(int _Value = (throw 1, 0)) {}
struct CRound83StrContext
{
	CRound83StrContext(char const *_pContext)
	{
		fg_Round83Helper();
	}
};
struct CRound83StrMember
{
	int m_Value = (throw 1, 0);
	CRound83StrMember(char const *_pContext) {}
};
DMibRunsToCompletion TCFuture<int> fg_Round83Discarded()
{
	TCFutureVector<int> Results;
	auto Join = co_await fg_AsyncDestroy
		(
			[&]() -> TCFuture<int>
			{
				co_await fg_AllDone(Results);
				co_return 0;
			}
		)
	;
	int Value = 42;
	if constexpr (true)
		fg_Read(Value) > Results;
	else
		throw 1;
	co_await fg_Move(Join);
	co_return 0;
}

// An allocation function that several call is looked into once, not once per
// path to it, which is exponential in their depth.
bool fg_Round84Choose() noexcept;
void *fg_Round84Allocate() noexcept;
template <int t_Depth>
struct TCRound84Allocated
{
	static void *operator new(decltype(sizeof(0)) _Size)
	{
		if (fg_Round84Choose())
			return new TCRound84Allocated<t_Depth - 1>;
		return new TCRound84Allocated<t_Depth - 1>;
	}
};
template <>
struct TCRound84Allocated<0>
{
	static void *operator new(decltype(sizeof(0)) _Size)
	{
		return fg_Round84Allocate();
	}
};
TCFuture<int> fg_Round84Factory()
{
	(void)new TCRound84Allocated<32>;
	return {};
}
DMibRunsToCompletion TCFuture<int> fg_Round84Allocations()
{
	TCFutureVector<int> Results;
	int Value = 0;
	auto Join = co_await fg_AsyncDestroy
		(
			[&]() -> TCFuture<int>
			{
				co_await fg_AllDone(Results);
				co_return 0;
			}
		)
	;
	fg_Read(Value) > Results;
	fg_Round84Factory();
	co_await fg_Move(Join);
	co_return 0;
}

// A pack of packs whose inner one is made by an operator that can throw, an
// awaiter whose `await_ready` can throw, and a chain of references made of
// conditionals, which is followed once for each.
TCFuture<long> fg_Round85Work();
TCFuture<int> operator+(TCFuture<long> _Left, TCFuture<long> _Right)
{
	throw 1;
}
struct CRound85Tagged
{
};
struct CRound85Pack
{
	TCFuture<int> m_First;
	struct CAwaiter
	{
		TCFuture<int> m_First;
		bool await_ready()
		{
			throw 1;
		}
		void await_suspend(std::coroutine_handle<> _Handle) noexcept;
		int await_resume() noexcept;
	};
	CAwaiter operator co_await () && noexcept
	{
		return {fg_Move(m_First)};
	}
};
CRound85Pack operator+(TCFuture<int> _First, CRound85Tagged _Second) noexcept;
DMibRunsToCompletion TCFuture<int> fg_Round85Ready()
{
	int Value = 42;
	co_await (fg_Read(Value) + CRound85Tagged{});
// CHECK-MESSAGES: :[[@LINE-1]]:12: warning: 'fg_Read' runs to completion, but isn't joined here, so it can run after what it refers to is gone; co_await it, or collect its future into a local that an fg_AsyncDestroy guard declared after the local awaits [malterlib-runs-to-completion]
	co_return 0;
}
#define DRound85Link(d_Index, d_Previous) int const &Value##d_Index = _bChoose ? Value##d_Previous : Value##d_Previous;
DMibRunsToCompletion TCFuture<int> fg_Round85Chain(bool _bChoose)
{
	int Value = 0;
	TCFutureVector<int> Results;
	auto Join = co_await fg_AsyncDestroy
		(
			[&]() -> TCFuture<int>
			{
				co_await fg_AllDone(Results);
				co_return 0;
			}
		)
	;
	int const &Value0 = Value;
	DRound85Link(1, 0) DRound85Link(2, 1) DRound85Link(3, 2) DRound85Link(4, 3)
	DRound85Link(5, 4) DRound85Link(6, 5) DRound85Link(7, 6) DRound85Link(8, 7)
	DRound85Link(9, 8) DRound85Link(10, 9) DRound85Link(11, 10) DRound85Link(12, 11)
	DRound85Link(13, 12) DRound85Link(14, 13) DRound85Link(15, 14) DRound85Link(16, 15)
	DRound85Link(17, 16) DRound85Link(18, 17) DRound85Link(19, 18) DRound85Link(20, 19)
	DRound85Link(21, 20) DRound85Link(22, 21) DRound85Link(23, 22) DRound85Link(24, 23)
	DRound85Link(25, 24) DRound85Link(26, 25) DRound85Link(27, 26) DRound85Link(28, 27)
	DRound85Link(29, 28) DRound85Link(30, 29) DRound85Link(31, 30) DRound85Link(32, 31)
	fg_Read(Value32) > Results;
	co_await fg_Move(Join);
	co_return 0;
}

// A reference made of itself in a branch a constant condition never takes
// is the other, and a null pointer refers to nothing.
DMibRunsToCompletion TCFuture<int> fg_Round86Work(int &_Value);
DMibRunsToCompletion TCFuture<int> fg_Round86Read(int *_pValue);
DMibRunsToCompletion TCFuture<int> fg_Round86Self(bool _bChoose)
{
	TCFutureVector<int> Results;
	auto Join = co_await fg_AsyncDestroy
		(
			[&]() -> TCFuture<int>
			{
				co_await fg_AllDone(Results);
				co_return 0;
			}
		)
	;
	int Local = 42;
	int &Alias = true ? Local : Alias;
	fg_Round86Work(Alias) > Results;
	int &Chosen = _bChoose ? Local : Chosen;
	fg_Round86Work(Chosen) > Results;
// CHECK-MESSAGES: :[[@LINE-1]]:17: warning: 'fg_Round86Work' runs to completion and is joined by 'Join', but this argument may not live until then; pass a local declared before 'Join' [malterlib-runs-to-completion]
	fg_Round86Read(_bChoose ? &Local : nullptr) > Results;
	co_await fg_Move(Join);
	co_return 0;
}

// A cleanup whose join can throw before it joins doesn't join.
struct CRound86Results
{
};
void operator > (TCFuture<int> &&_Future, CRound86Results &_Results) noexcept;
TCFuture<int> fg_AllDone(CRound86Results &_Results)
{
	throw 1;
}
// An early await of the guard whose awaiter can throw, like an
// `await_transform` of the promise that throws, may not join.
struct CRound88Task
{
	struct promise_type
	{
		CRound88Task get_return_object();
		std::suspend_never initial_suspend();
		std::suspend_never final_suspend() noexcept;
		void return_void();
		void unhandled_exception();

		template <typename t_CType>
		t_CType &&await_transform(t_CType &&_Awaited) noexcept
		{
			return static_cast<t_CType &&>(_Awaited);
		}
		CAwaiter await_transform(CAsyncDestroy &&_Guard)
		{
			throw 1;
		}
	};
};
DMibRunsToCompletion TCFuture<int> fg_Round88Read(int &_Value);
// Replacing the collection in parentheses replaces it too, and a string made
// of a literal whose constructor throws a bad cast can leave before the join.
DMibRunsToCompletion TCFuture<int> fg_Round89Read(int const &_Value);
DMibRunsToCompletion TCFuture<int> fg_Round89Parenthesized()
{
	TCFutureVector<int> Results;
	int Local = 0;
	auto Join = co_await fg_AsyncDestroy
		(
			[&]() -> TCFuture<int>
			{
				co_return co_await fg_AllDone(Results);
			}
		)
	;
	fg_Round89Read(Local) > Results;
// CHECK-MESSAGES: :[[@LINE-1]]:2: warning: 'fg_Round89Read' runs to completion, but isn't joined here, so it can run after what it refers to is gone; co_await it, or collect its future into a local that an fg_AsyncDestroy guard declared after the local awaits [malterlib-runs-to-completion]
	(((((Results))))) = TCFutureVector<int>{};
	co_await fg_Move(Join);
	co_return 0;
}
struct CRound89Base
{
	virtual ~CRound89Base() = default;
};
struct CRound89Derived : CRound89Base
{
};
struct CRound89StrContext
{
	CRound89StrContext(char const *_pText)
	{
		CRound89Base Base;
		(void)dynamic_cast<CRound89Derived &>(Base);
	}
};
// A pointer assigned through a reference to it, to a local that is gone
// before the join.
TCFuture<int> fg_Round90Work();
DMibRunsToCompletion TCFuture<int> fg_Round90Alias()
{
	TCFutureVector<int> Results;
	int *pValue = nullptr;
	auto Join = co_await fg_AsyncDestroy
		(
			[&]() -> TCFuture<int>
			{
				co_await fg_AllDone(Results);
				co_return 0;
			}
		)
	;
	{
		int Inner = 42;
		auto &Alias = pValue;
		Alias = &Inner;
		g_ConcurrentDispatch / [&pValue]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:28: warning: a TCFuture coroutine lambda captures 'pValue' by reference but is not called directly where 'pValue' is, so it can run after 'pValue' is gone; capture by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
			{
				co_await fg_Round90Work();
				co_return *pValue;
			}
			> Results
		;
	}
	co_await fg_Move(Join);
	co_return 0;
}

// A suspension that can't be reached isn't the first.
DMibRunsToCompletionEntry TCFuture<int> fg_Round90Unreachable()
{
	if (false)
		co_await fg_Round90Work();
	co_await fg_ContinueRunningOnActor(fg_ConcurrentActor());
	co_return 0;
}

// A default argument of a method of the awaiter that throws before the call
// is joined.
struct CRound91Awaiter
{
	TCAwaiter<int> m_Wrapped;
	bool await_ready(int _Unused = (throw 42, 0)) const noexcept
	{
		return m_Wrapped.await_ready();
	}
	void await_suspend(std::coroutine_handle<> _Handle) const noexcept
	{
		m_Wrapped.await_suspend(_Handle);
	}
	int await_resume() const noexcept
	{
		return m_Wrapped.await_resume();
	}
};
struct CRound91Task
{
	struct promise_type
	{
		CRound91Task get_return_object();
		std::suspend_never initial_suspend();
		std::suspend_never final_suspend() noexcept;
		void return_void();
		void unhandled_exception();
		CRound91Awaiter await_transform(TCFuture<int> &&_Future) noexcept
		{
			return {fg_Move(_Future).operator co_await()};
		}
	};
};
DMibRunsToCompletion TCFuture<int> fg_Round91Read(int &_Value);
// The taken branch of a constant condition runs on every path.
DMibRunsToCompletionEntry TCFuture<int> fg_Round91Constant()
{
	if (true)
		co_await fg_ContinueRunningOnActor(fg_ConcurrentActor());
	co_return 0;
}

// Through a long chain of copies of a pointer to a local that is gone before
// the join.
DMibRunsToCompletion TCFuture<int> fg_Round92Chain()
{
	int *pValue = nullptr;
	TCFutureVector<int> Results;
	auto Join = co_await fg_AsyncDestroy
		(
			[&]() -> TCFuture<int>
			{
				co_await fg_AllDone(Results);
				co_return 0;
			}
		)
	;
	{
		int Local = 123;
		int *p0 = &Local, *p1 = p0, *p2 = p1, *p3 = p2, *p4 = p3, *p5 = p4,
			*p6 = p5, *p7 = p6, *p8 = p7;
		pValue = p8;
		fg_CallSafe
			(
				[&pValue]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:7: warning: a TCFuture coroutine lambda uses 'pValue', which it captures by reference, after a suspension, when 'pValue' may be gone; capture by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
				{
					co_await fg_Round91Read(*pValue);
					co_return *pValue;
				}
			)
			> Results
		;
	}
	co_await fg_Move(Join);
	co_return 0;
}

// An awaiter whose constructor, or the default argument of its await_ready,
// can throw before the future is joined.
DMibRunsToCompletion TCFuture<int> fg_Round93Work(int &o_Value);

struct CRound93Wrapped
{
	TCFuture<int> m_Future;
};
CRound93Wrapped operator % (TCFuture<int> &&_Future, int _Flags) noexcept
{
	return {static_cast<TCFuture<int> &&>(_Future)};
}

struct CRound93ThrowingAwaiter
{
	CRound93ThrowingAwaiter(TCFuture<int> &&_Future)
		: m_Future(static_cast<TCFuture<int> &&>(_Future))
	{
		throw 1;
	}
	bool await_ready() const noexcept { return false; }
	void await_suspend(std::coroutine_handle<> _Handle) const noexcept {}
	int await_resume() const noexcept { return 0; }

	TCFuture<int> m_Future;
};

CRound93ThrowingAwaiter operator co_await (CRound93Wrapped _Wrapped)
{
	return {static_cast<TCFuture<int> &&>(_Wrapped.m_Future)};
}

struct CRound93Wrapped2
{
	TCFuture<int> m_Future;
};
CRound93Wrapped2 operator % (TCFuture<int> &&_Future, bool _bFlag) noexcept
{
	return {static_cast<TCFuture<int> &&>(_Future)};
}

struct CRound93Throws
{
	CRound93Throws()
	{
		throw 1;
	}
};

struct CRound93DefaultAwaiter
{
	bool await_ready(CRound93Throws _Throws = {}) const noexcept { return false; }
	void await_suspend(std::coroutine_handle<> _Handle) const noexcept {}
	int await_resume() const noexcept { return 0; }

	TCFuture<int> m_Future;
};

CRound93DefaultAwaiter operator co_await (CRound93Wrapped2 _Wrapped) noexcept
{
	return {static_cast<TCFuture<int> &&>(_Wrapped.m_Future)};
}

// Destroying what a handler catches by value can throw out of the handler.
struct CRound94Error
{
	CRound94Error() noexcept = default;
	CRound94Error(CRound94Error const &_Other) noexcept
		: m_bCopied(true)
	{
	}
	~CRound94Error() noexcept(false)
	{
		if (m_bCopied)
			throw 7;
	}

	bool m_bCopied = false;
};

void fg_Round94Fail()
{
	throw CRound94Error{};
}
DMibRunsToCompletion TCFuture<int> fg_Round94Read(int &o_Value);

// Making the awaiter of a flag can throw, by a promise that transforms it.
struct CRound94Tag
{
};
namespace std
{
	template <>
	struct coroutine_traits<TCFuture<int>, CRound94Tag>
	{
		struct promise_type : TCFuture<int>::promise_type
		{
			using TCFuture<int>::promise_type::await_transform;
			CAwaiter await_transform(ECoroutineFlag _Flag)
			{
				throw 1;
			}
		};
	};
}

DMibRunsToCompletion TCFuture<int> fg_Round94ThrowingFlag(CRound94Tag _Tag)
{
	TCFutureVector<int> Results;
	auto Join = co_await fg_AsyncDestroy
		(
			[&]() -> TCFuture<int>
			{
				co_await fg_AllDone(Results);
				co_return 0;
			}
		)
	;
	int Local = 1;
	fg_Round94Read(Local) > Results;
// CHECK-MESSAGES: :[[@LINE-1]]:17: warning: 'fg_Round94Read' runs to completion and is joined by 'Join', but this argument may not live until then; pass a local declared before 'Join' [malterlib-runs-to-completion]
	co_await ECoroutineFlag_CaptureExceptions;
	co_await fg_Move(Join);
	co_return 0;
}

// A guard made in a loop is a new one each time, which joins what was started
// in that pass.
DMibRunsToCompletion TCFuture<int> fg_Round94FreshGuards()
{
	for (int iPass = 0; iPass < 2; ++iPass)
	{
		int Local = 1;
		TCFutureVector<int> Results;
		auto Join = co_await fg_AsyncDestroy
			(
				[&]() -> TCFuture<int>
				{
					co_await fg_AllDone(Results);
					co_return 0;
				}
			)
		;
		fg_Round94Read(Local) > Results;
		co_await fg_Move(Join);
	}
	co_return 0;
}

// An await of a future that aborts runs after the call it is written before,
// also in a `try` of a `catch (...)`, and destroying the awaiter an
// `operator co_await` makes can throw.
TCFuture<int> fg_Round95Work();
DMibRunsToCompletion TCFuture<int> fg_Round95Read(int &o_Value)
{
	co_await fg_Round95Work();
	co_return o_Value;
}

DMibRunsToCompletion TCFuture<int> fg_Round95Caught()
{
	int Value = 42;
	int Slots[1] = {};
	TCFutureVector<int> Results;
	auto Join = co_await fg_AsyncDestroy
		(
			[&]() -> TCFuture<int>
			{
				co_await fg_AllDone(Results);
				co_return 0;
			}
		)
	;
	try
	{
		Slots[co_await fg_Round95Work()] = (fg_Round95Read(Value) > Results, 0);
// CHECK-MESSAGES: :[[@LINE-1]]:54: warning: 'fg_Round95Read' runs to completion and is joined by 'Join', but this argument may not live until then; pass a local declared before 'Join' [malterlib-runs-to-completion]
	}
	catch (...)
	{
	}
	co_await fg_Move(Join);
	co_return 0;
}

struct CRound95Ready : std::suspend_never
{
	~CRound95Ready() noexcept(false)
	{
		throw 1;
	}
};

struct CRound95Awaitable
{
	CRound95Ready operator co_await() const noexcept
	{
		return {};
	}
};

// A drain whose context can throw before it is awaited doesn't join.
DMibRunsToCompletion TCFuture<int> fg_Round96Read(int const &_Value);
char const *fg_Round96Context()
{
	throw 1;
}

// An override in an instantiation is checked against what it overrides, and
// a context that can throw by a cast to a reference doesn't join.
struct CRound97Base
{
	virtual TCFuture<int> f_Run(int &o_Value) = 0;
};

template <typename t_CBase>
struct TCRound97Worker : t_CBase
{
	DMibRunsToCompletion TCFuture<int> f_Run(int &o_Value) override
// CHECK-MESSAGES: :[[@LINE-1]]:37: warning: 'f_Run' runs to completion, but overrides 'CRound97Base::f_Run', which doesn't, so the callers that call it through 'CRound97Base::f_Run' aren't checked to join it; declare 'CRound97Base::f_Run' with DMibRunsToCompletion too [malterlib-runs-to-completion]
	{
		co_return o_Value;
	}
};

TCRound97Worker<CRound97Base> g_Round97Worker;

struct CRound97Polymorphic
{
	virtual ~CRound97Polymorphic() = default;
};
struct CRound97Derived : CRound97Polymorphic
{
	char const *m_pText;
};
DMibRunsToCompletion TCFuture<int> fg_Round97Read(int &o_Value);

// An await with context can abort the coroutine before the early join.
TCFuture<int> fg_Round99Fail();
TCFutureWithExceptionTransformer<int> operator % (TCFuture<int> &&_Future, char const *_pContext);

DMibRunsToCompletion TCFuture<int> fg_Round99Read(int &_Value)
{
	co_await TCFuture<int>{};
	co_return _Value;
}

DMibRunsToCompletion TCFuture<int> fg_Round99Context()
{
	TCFutureVector<int> Results;
	auto Join = co_await fg_AsyncDestroy
		(
			[&]() -> TCFuture<int>
			{
				co_await fg_AllDone(Results);
				co_return 0;
			}
		)
	;
	int Local = 42;
	fg_Round99Read(Local) > Results;
// CHECK-MESSAGES: :[[@LINE-1]]:17: warning: 'fg_Round99Read' runs to completion and is joined by 'Join', but this argument may not live until then; pass a local declared before 'Join' [malterlib-runs-to-completion]
	co_await (fg_Round99Fail() % "Context");
	co_await fg_Move(Join);
	co_return 0;
}

// Capturing exceptions in a discarded branch doesn't capture them.
DMibRunsToCompletion TCFuture<int> fg_Round99Discarded()
{
	TCFutureVector<int> Results;
	auto Join = co_await fg_AsyncDestroy
		(
			[&]() -> TCFuture<int>
			{
				co_await fg_AllDone(Results);
				co_return 0;
			}
		)
	;
	int Local = 42;
	fg_Round99Read(Local) > Results;
	if constexpr (false)
		co_await g_CaptureExceptions;
	co_await fg_Move(Join);
	co_return 0;
}

// Collected through a local reference to the collection.
DMibRunsToCompletion TCFuture<int> fg_Round100Alias(int &_Value)
{
	TCFutureVector<int> Results;
	auto &Alias = Results;
	auto Guard = co_await fg_AsyncDestroy
		(
			[&]() -> TCFuture<int>
			{
				co_await fg_AllDone(Results);
				co_return 0;
			}
		)
	;
	fg_Round99Read(_Value) > Alias;
	co_return 0;
}

// Awaiting the cleanup of another guard can abort the coroutine.
DMibRunsToCompletion TCFuture<int> fg_Round101Cleanup()
{
	TCFutureVector<int> Results;
	auto Join = co_await fg_AsyncDestroy
		(
			[&]() -> TCFuture<int>
			{
				co_await fg_AllDone(Results);
				co_return 0;
			}
		)
	;
	int Local = 1;
	fg_Round99Read(Local) > Results;
// CHECK-MESSAGES: :[[@LINE-1]]:17: warning: 'fg_Round99Read' runs to completion and is joined by 'Join', but this argument may not live until then; pass a local declared before 'Join' [malterlib-runs-to-completion]
	auto Cleanup = co_await fg_AsyncDestroy
		(
			[]() -> TCFuture<int>
			{
				co_return co_await fg_Round99Fail();
			}
		)
	;
	co_await fg_Move(Cleanup);
	co_await fg_Move(Join);
	co_return 0;
}

// Collected with a member function that may throw.
struct CRound102Results
{
	TCFutureVector<int> m_Futures;
	void f_Insert(TCFuture<int> &&_Future)
	{
		fg_Move(_Future) > m_Futures;
	}
};
TCFuture<int> fg_AllDone(CRound102Results &_Results);

DMibRunsToCompletion TCFuture<int> fg_Round102Insert()
{
	int Value = 42;
	CRound102Results Results;
	auto Join = co_await fg_AsyncDestroy
		(
			[&]() -> TCFuture<int>
			{
				co_await fg_AllDone(Results);
				co_return 0;
			}
		)
	;
	Results.f_Insert(fg_Round99Read(Value));
	co_await fg_Move(Join);
	co_return 0;
}

// An await that unwraps an error can abort before the early join.
namespace NMib::NConcurrency::NUnwrap
{
	template <typename t_CType>
	struct TCUnwrapAwaiter
	{
		bool await_ready() noexcept;
		bool await_suspend(std::coroutine_handle<> _Handle) noexcept;
		t_CType await_resume() noexcept;
	};
}
NMib::NConcurrency::NUnwrap::TCUnwrapAwaiter<int> fg_Round103Unwrap();

DMibRunsToCompletion TCFuture<int> fg_Round103Unwrapped()
{
	int Local = 42;
	TCFutureVector<int> Results;
	auto Join = co_await fg_AsyncDestroy
		(
			[&]() -> TCFuture<int>
			{
				co_await fg_AllDone(Results).f_Wrap();
				co_return 0;
			}
		)
	;
	fg_Round99Read(Local) > Results;
// CHECK-MESSAGES: :[[@LINE-1]]:17: warning: 'fg_Round99Read' runs to completion and is joined by 'Join', but this argument may not live until then; pass a local declared before 'Join' [malterlib-runs-to-completion]
	co_await fg_Round103Unwrap();
	co_await fg_Move(Join);
	co_return 0;
}

// Joined by an fg_AllDone of the moved collection.
namespace NMib::NConcurrency
{
	template <typename tf_CType>
	TCFuture<int> fg_AllDone(TCFutureVector<tf_CType> &&_Results);
}

DMibRunsToCompletion TCFuture<int> fg_Round103Moved(int &_Value)
{
	TCFutureVector<int> Results;
	auto Join = co_await fg_AsyncDestroy
		(
			[&]() -> TCFuture<int>
			{
				co_await fg_AllDone(fg_Move(Results)).f_Wrap();
				co_return 0;
			}
		)
	;
	fg_Round99Read(_Value) > Results;
	co_await fg_Move(Join);
	co_return 0;
}

// Awaiting the cleanups of the guards of the coroutine can abort it.
namespace NMib::NConcurrency
{
	struct CAsyncDestroyHelper
	{
		bool await_ready() const noexcept;
		void await_suspend(std::coroutine_handle<> _Handle) const noexcept;
		void await_resume() const noexcept;
	};
	extern CAsyncDestroyHelper const g_AsyncDestroy;
}

DMibRunsToCompletion TCFuture<int> fg_Round104AsyncDestroy()
{
	TCFutureVector<int> Results;
	auto Join = co_await fg_AsyncDestroy
		(
			[&]() -> TCFuture<int>
			{
				co_await fg_AllDone(Results).f_Wrap();
				co_return 0;
			}
		)
	;
	int Local = 0;
	fg_Round99Read(Local) > Results;
// CHECK-MESSAGES: :[[@LINE-1]]:17: warning: 'fg_Round99Read' runs to completion and is joined by 'Join', but this argument may not live until then; pass a local declared before 'Join' [malterlib-runs-to-completion]
	co_await g_AsyncDestroy;
	co_await fg_Move(Join);
	co_return 0;
}

// What a pointer captured by reference is pointed at through
// `std::addressof`, a cast and a conditional.
DMibRunsToCompletion TCFuture<int> fg_Round104AddressOf()
{
	int *pValue = nullptr;
	TCFutureVector<int> Results;
	auto Join = co_await fg_AsyncDestroy
		(
			[&]() -> TCFuture<int>
			{
				co_await fg_AllDone(Results).f_Wrap();
				co_return 0;
			}
		)
	;
	{
		int Local = 0;
		pValue = __builtin_addressof(Local);
		fg_CallSafe
			(
				[&pValue]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:7: warning: a TCFuture coroutine lambda uses 'pValue', which it captures by reference, after a suspension, when 'pValue' may be gone; capture by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
				{
					co_await TCFuture<int>{};
					co_return *pValue;
				}
			) > Results
		;
	}
	co_await fg_Move(Join);
	co_return 0;
}

DMibRunsToCompletion TCFuture<int> fg_Round104CastAssigned()
{
	int *pValue = nullptr;
	TCFutureVector<int> Results;
	auto Join = co_await fg_AsyncDestroy
		(
			[&]() -> TCFuture<int>
			{
				co_await fg_AllDone(Results).f_Wrap();
				co_return 0;
			}
		)
	;
	{
		int Local = 0;
		static_cast<int *&>(pValue) = &Local;
		fg_CallSafe
			(
				[&pValue]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:7: warning: a TCFuture coroutine lambda uses 'pValue', which it captures by reference, after a suspension, when 'pValue' may be gone; capture by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
				{
					co_await TCFuture<int>{};
					co_return *pValue;
				}
			) > Results
		;
	}
	co_await fg_Move(Join);
	co_return 0;
}

// A view copied from a view of a local of an inner scope.
struct CRound105StrSpan
{
	int const *m_pData = nullptr;
	CRound105StrSpan() = default;
	CRound105StrSpan(int const *_pData, int _Size)
		: m_pData(_pData)
	{
	}
	int operator[](int _Index) const
	{
		return m_pData[_Index];
	}
};

DMibRunsToCompletion TCFuture<int> fg_Round105CopiedView()
{
	TCFutureVector<int> Results;
	auto Join = co_await fg_AsyncDestroy
		(
			[&]() -> TCFuture<int>
			{
				co_await fg_AllDone(Results).f_Wrap();
				co_return 0;
			}
		)
	;
	CRound105StrSpan Outer;
	{
		int Inner[1] = {42};
		CRound105StrSpan Span(Inner, 1);
		Outer = Span;
		fg_CallSafe
			(
				[&Outer]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:7: warning: a TCFuture coroutine lambda uses 'Outer', which it captures by reference, after a suspension, when 'Outer' may be gone; capture by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
				{
					co_await TCFuture<int>{};
					co_return Outer[0];
				}
			) > Results
		;
	}
	co_await fg_Move(Join);
	co_return 0;
}

// A pointer assigned the address of what another pointer points to.
DMibRunsToCompletion TCFuture<int> fg_Round105AddressOfDereference()
{
	TCFutureVector<int> Results;
	auto Join = co_await fg_AsyncDestroy
		(
			[&]() -> TCFuture<int>
			{
				co_await fg_AllDone(Results).f_Wrap();
				co_return 0;
			}
		)
	;
	int *pValue = nullptr;
	{
		int Inner = 42;
		int *pInner = &Inner;
		pValue = &*pInner;
		fg_CallSafe
			(
				[&pValue]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:7: warning: a TCFuture coroutine lambda uses 'pValue', which it captures by reference, after a suspension, when 'pValue' may be gone; capture by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
				{
					co_await TCFuture<int>{};
					co_return *pValue;
				}
			) > Results
		;
	}
	co_await fg_Move(Join);
	co_return 0;
}

// A return that never runs doesn't leave before the early join.
DMibRunsToCompletion TCFuture<int> fg_Round105Read(int &_Value)
{
	co_await TCFuture<int>{};
	co_return _Value;
}

DMibRunsToCompletion TCFuture<int> fg_Round105NeverReturns()
{
	TCFutureVector<int> Results;
	auto Join = co_await fg_AsyncDestroy
		(
			[&]() -> TCFuture<int>
			{
				co_await fg_AllDone(Results).f_Wrap();
				co_return 0;
			}
		)
	;
	int Value = 42;
	fg_Round105Read(Value) > Results;
	if (false)
		co_return 0;
	co_await fg_Move(Join);
	co_return 0;
}

// A view assigned through a local reference to it.
DMibRunsToCompletion TCFuture<int> fg_Round106ViewAlias()
{
	TCFutureVector<int> Results;
	CRound105StrSpan View;
	auto &Alias = View;
	auto Join = co_await fg_AsyncDestroy
		(
			[&]() -> TCFuture<int>
			{
				co_await fg_AllDone(Results).f_Wrap();
				co_return 0;
			}
		)
	;
	{
		int Local[1] = {42};
		Alias = CRound105StrSpan(Local, 1);
		fg_CallSafe
			(
				[&View]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:7: warning: a TCFuture coroutine lambda uses 'View', which it captures by reference, after a suspension, when 'View' may be gone; capture by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
				{
					co_await TCFuture<int>{};
					co_return View[0];
				}
			) > Results
		;
	}
	co_await fg_Move(Join);
	co_return 0;
}

// A view copied through more views than were followed before.
DMibRunsToCompletion TCFuture<int> fg_Round106ViewChain()
{
	TCFutureVector<int> Results;
	CRound105StrSpan View;
	auto Join = co_await fg_AsyncDestroy
		(
			[&]() -> TCFuture<int>
			{
				co_await fg_AllDone(Results).f_Wrap();
				co_return 0;
			}
		)
	;
	{
		int Local[1] = {42};
		auto View0 = CRound105StrSpan(Local, 1), View1 = View0, View2 = View1, View3 = View2, View4 = View3,
			View5 = View4, View6 = View5, View7 = View6, View8 = View7;
		View = View8;
		fg_CallSafe
			(
				[&View]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:7: warning: a TCFuture coroutine lambda uses 'View', which it captures by reference, after a suspension, when 'View' may be gone; capture by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
				{
					co_await TCFuture<int>{};
					co_return View[0];
				}
			) > Results
		;
	}
	co_await fg_Move(Join);
	co_return 0;
}

// An await that a constant condition, or a constant left side of `||`,
// never runs doesn't leave before the early join.
DMibRunsToCompletion TCFuture<int> fg_Round109NeverAwaits()
{
	TCFutureVector<int> Results;
	auto Join = co_await fg_AsyncDestroy
		(
			[&]() -> TCFuture<int>
			{
				co_await fg_AllDone(Results).f_Wrap();
				co_return 0;
			}
		)
	;
	int Value = 42;
	fg_Round105Read(Value) > Results;
	int Unused = false ? co_await TCFuture<int>{} : 0;
	bool bUnused = true || (co_await TCFuture<int>{}, false);
	co_await fg_Move(Join);
	co_return Unused + bUnused;
}

// A pointer that can point to either of two locals, captured by copy, refers
// to the one declared last.
DMibRunsToCompletion TCFuture<int> fg_Round110EitherLocal(bool _bLate)
{
	int Early = 0;
	int *pValue = &Early;
	TCFutureVector<int> Results;
	auto Join = co_await fg_AsyncDestroy
		(
			[&Results]() -> TCFuture<int>
			{
				co_await fg_AllDone(Results).f_Wrap();
				co_return 0;
			}
		)
	;
	{
		int Late = 1;
		if (_bLate)
			pValue = &Late;
		fg_CallSafe
			(
				[pValue]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:6: warning: a TCFuture coroutine lambda uses 'pValue', which points to 'Late', after a suspension, when 'Late' may be gone; capture a value or a shared pointer, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
				{
					co_await TCFuture<int>{};
					co_return *pValue;
				}
			) > Results
		;
	}
	co_await fg_Move(Join);
	co_return 0;
}

// The awaiter of a pack that unwraps, made explicitly, aborts the coroutine
// when one fails.
namespace NRound111
{
	template <bool t_bUnwrap, typename t_CTransform, typename... tp_CFutures>
	struct TCFuturePackAwaiter
	{
		bool await_ready() const noexcept
		{
			return false;
		}
		void await_suspend(std::coroutine_handle<> _Handle);
		int await_resume() noexcept(!t_bUnwrap);
	};
}
struct CRound111Pack
{
	NRound111::TCFuturePackAwaiter<true, int, TCFuture<int>, TCFuture<int>> operator co_await () &&;
};
CRound111Pack operator - (TCFuture<int> &&_Left, TCFuture<int> &&_Right);
DMibRunsToCompletion TCFuture<int> fg_Round111Pack()
{
	int Value = 7;
	TCFutureVector<int> Results;
	auto Join = co_await fg_AsyncDestroy
		(
			[&]() -> TCFuture<int>
			{
				co_await fg_AllDone(Results);
				co_return 0;
			}
		)
	;
	fg_Read(Value) > Results;
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: 'fg_Read' runs to completion and is joined by 'Join', but this argument may not live until then; pass a local declared before 'Join' [malterlib-runs-to-completion]
	auto Awaiter = (fg_Work(0) - fg_Work(1)).operator co_await();
	co_await fg_Move(Awaiter);
	co_await fg_Move(Join);
	co_return 0;
}

// A suspension that can't run isn't the first.
DMibRunsToCompletionEntry TCFuture<int> fg_Round112Entry()
{
	while (false)
		co_await fg_Work(0);
	co_await fg_ContinueRunningOnActor(fg_ConcurrentActor());
	co_return 0;
}

// Round 113: the elements of a braced initializer after the join run after it
namespace NRound113Braced
{
DMibRunsToCompletion TCFuture<int> read(int &);
TCFuture<int> work();
struct Pair { int a, b; };
DMibRunsToCompletion TCFuture<int> caller() {
  TCFutureVector<int> results;
  auto guard = co_await fg_AsyncDestroy([&]() -> TCFuture<int> {
    co_await fg_AllDone(results);
    co_return 0;
  });
  int local = 0;
  read(local) > results;
  Pair values{(co_await fg_Move(guard), 0), co_await work()};
  co_return values.a;
}
}

// Round 113: a switch kept and never awaited doesn't switch
namespace NRound113Unawaited
{
DMibRunsToCompletion TCFuture<int> f(TCActor<> other) {
  auto prepared = fg_ContinueRunningOnActor(other);
  co_return 0;
}
}

// Round 114: a reference to a pointer is the pointer it is bound to
namespace NRound114RefPointer
{
TCFuture<int> work();
DMibRunsToCompletion TCFuture<int> f() {
  int *p = nullptr;
  TCFutureVector<int> results;
  auto join = co_await fg_AsyncDestroy([&]() -> TCFuture<int> { co_await fg_AllDone(results); co_return 0; });
  {
    int late = 1;
    int *source = &late;
    int *&alias = source;
    p = alias;
    g_Dispatch / [&p]() -> TCFuture<int> { co_await work(); co_return *p; } > results;
// CHECK-MESSAGES: :[[@LINE-1]]:20: warning: a TCFuture coroutine lambda captures 'p' by reference but is not called directly where 'p' is, so it can run after 'p' is gone; capture by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
  }
  co_await fg_Move(join);
  co_return 0;
}
}

// Round 114: the awaiter of a bound actor call unwraps failures
namespace NRound114Bound
{
template<class T, class Call, bool Unwrap, class Transform = int>
struct TCBoundActorCallAwaiter { bool await_ready() const noexcept { return false; } void await_suspend(std::coroutine_handle<>) const noexcept; T await_resume() noexcept; };
struct TCBoundActorCall { TCBoundActorCallAwaiter<int, TCBoundActorCall, true> operator co_await() &&; };
TCBoundActorCall fg_Bind();
DMibRunsToCompletion TCFuture<int> fg_Read(int const &value);
DMibRunsToCompletion TCFuture<int> fg_Run() {
  TCFutureVector<int> results;
  auto join = co_await fg_AsyncDestroy([&]() -> TCFuture<int> { co_await fg_AllDone(results); co_return 0; });
  int local = 1;
  fg_Read(local) > results;
// CHECK-MESSAGES: :[[@LINE-1]]:11: warning: 'fg_Read' runs to completion and is joined by 'join', but this argument may not live until then; pass a local declared before 'join' [malterlib-runs-to-completion]
  co_await fg_Bind().operator co_await();
  co_await fg_Move(join);
  co_return 0;
}
}

// A cleanup awaiting the collection through a reference alias joins it.
namespace NRound115AliasJoin {
DMibRunsToCompletion TCFuture<int> fg_Read(int &Value);
DMibRunsToCompletion TCFuture<int> fg_Run() {
  TCFutureVector<int> Results;
  int Local = 0;
  auto &Alias = Results;
  auto Join = co_await fg_AsyncDestroy([&Alias]() -> TCFuture<int> {
    co_return co_await fg_AllDone(Alias);
  });
  fg_Read(Local) > Results;
  co_await fg_Move(Join);
  co_return 0;
}
}

// Many calls joined by the same guard are decided once.
namespace NRound115ManyJoined {
#define TWICE(x) x x
DMibRunsToCompletion TCFuture<int> work(int &v);
DMibRunsToCompletion TCFuture<int> run(int &v) {
  TCFutureVector<int> r;
  auto join = co_await fg_AsyncDestroy([&]() -> TCFuture<int> {
    co_return co_await fg_AllDone(r);
  });
  TWICE(TWICE(TWICE(TWICE(TWICE(TWICE(
    TWICE(TWICE(TWICE(TWICE(TWICE(TWICE(work(v) > r;)))))
  )))))))
  co_return 0;
}
}

// A transfer of a class derived from CCoroutineTransferOwnership is to an actor that can't be told.
namespace NRound116DerivedTransfer {
struct Transfer : CCoroutineTransferOwnership {};
[[clang::annotate("malterlib-runs-to-completion")]]
TCFuture<int> also_bad(Transfer transfer, int &value) {
  co_await fg_Move(transfer);
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: code that runs to completion switches here to an actor that can't be told, which may be destroyed while it runs and abort it; switch with fg_ContinueRunningOnActor to a concurrent actor [malterlib-runs-to-completion]
  co_return value;
}
}

// Exceptions captured in a lambda capture initializer, which runs in the coroutine, are captured by it.
namespace NRound116CaptureInit {
TCFuture<int> work();
void throwing_call();
DMibRunsToCompletion TCFuture<int> read(int &value) {
  co_await work();
  co_return value;
}
DMibRunsToCompletion TCFuture<int> bad() {
  auto scope =
      [flag = (co_await ECoroutineFlag_CaptureExceptions, 0)] {};
  TCFutureVector<int> results;
  auto guard = co_await fg_AsyncDestroy([&]() -> TCFuture<int> {
    co_await fg_AllDone(results);
    co_return 0;
  });
  int value = 42;
  read(value) > results;
// CHECK-MESSAGES: :[[@LINE-1]]:8: warning: 'read' runs to completion and is joined by 'guard', but this argument may not live until then; pass a local declared before 'guard' [malterlib-runs-to-completion]
  throwing_call();
  co_await fg_Move(guard);
  co_return 0;
}
}

// A temporary resume registration earlier in the full expression can abort the coroutine.
namespace NRound117TemporaryRegistration {
TCFuture<int> pause();
bool cancelled();
DMibRunsToCompletion TCFuture<int> borrow(int &Value) {
  co_await pause().f_Wrap();
  co_return Value;
}
DMibRunsToCompletion TCFuture<int> test() {
  TCFutureVector<int> Results;
  int Local = 0;
  auto Join = co_await fg_AsyncDestroy([&]() -> TCFuture<int> {
    co_await fg_AllDone(Results);
    co_return 0;
  });
  ((void)(co_await fg_OnResume([] {
     return cancelled() ? fg_MakeError("Cancelled")
                        : CExceptionPointer{};
   })),
   borrow(Local) > Results,
// CHECK-MESSAGES: :[[@LINE-1]]:11: warning: 'borrow' runs to completion and is joined by 'Join', but this argument may not live until then; pass a local declared before 'Join' [malterlib-runs-to-completion]
   co_await fg_ContinueRunningOnActor(fg_ConcurrentActor()));
  co_await fg_Move(Join);
  co_return 0;
}
}

// A default argument runs at each call that leaves it out.
namespace NRound117DefaultArgument {
TCFuture<int> pause();
DMibRunsToCompletion TCFuture<int> borrow(int &Value) {
  co_await pause().f_Wrap();
  co_return Value;
}
int *Current;
void drop(TCFuture<int> Future = borrow(*Current)) {}
// CHECK-MESSAGES: :[[@LINE-1]]:34: warning: 'borrow' runs to completion, but is called here where the code doesn't, so it can be aborted while 'borrow' refers to its data; call it from a function that runs to completion (DMibRunsToCompletion), or one that begins to (DMibRunsToCompletionEntry) [malterlib-runs-to-completion]
void caller() {
  int Local = 0;
  Current = &Local;
  drop();
}
}

// Lambdas nested in default arguments are traversed once by descendant matchers.
namespace NRound117NestedDefaults {
#define L(X) [](int Value = (X)) { return Value; }(0)
DMibRunsToCompletion TCFuture<int> f(TCActor<> Other) {
  auto Switch = fg_ContinueRunningOnActor(Other);
  int v = L(L(L(L(L(L(L(L(L(L(L(L(7))))))))))));
  co_return v;
}
}

// An await of the guard in a branch that never runs is not the early join.
namespace NRound117UnreachableJoin {
DMibRunsToCompletion TCFuture<int> borrow(int &Value);
DMibRunsToCompletion TCFuture<int> test() {
  TCFutureVector<int> Results;
  int Local = 0;
  auto Join = co_await fg_AsyncDestroy([&]() -> TCFuture<int> {
    co_await fg_AllDone(Results);
    co_return 0;
  });
  if (false)
    co_await fg_Move(Join);
  borrow(Local) > Results;
  co_await fg_Move(Join);
  co_return 0;
}
}

// What a pointer points to through an arrow is where the pointer points.
namespace NRound118ArrowPointee {
TCFuture<int> fg_Work(int);
struct Data { int value; };

DMibRunsToCompletion TCFuture<int> test() {
  TCFutureVector<int> Results;
  int *p = nullptr;
  auto Join = co_await fg_AsyncDestroy([&]() -> TCFuture<int> {
    co_await fg_AllDone(Results);
    co_return 0;
  });
  {
    Data Local{};
    auto *pLocal = &Local;
    p = &pLocal->value;
    fg_CallSafe([&p]() -> TCFuture<int> {
// CHECK-MESSAGES: :[[@LINE-1]]:19: warning: a TCFuture coroutine lambda uses 'p', which it captures by reference, after a suspension, when 'p' may be gone; capture by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
      co_await fg_Work(0);
      co_return *p;
    }) > Results;
  } // Local dies before the join.
  co_await fg_Move(Join);
  co_return 0;
}
}

// An assignment after the join can't change what the joined coroutine used.
namespace NRound118AfterJoin {
TCFuture<int> fg_Work(int);

DMibRunsToCompletion TCFuture<int> test() {
  TCFutureVector<int> Results;
  int Value = 1;
  int *p = &Value;
  auto Join = co_await fg_AsyncDestroy([&]() -> TCFuture<int> {
    co_await fg_AllDone(Results);
    co_return 0;
  });
  fg_CallSafe([&p]() -> TCFuture<int> {
    co_await fg_Work(0);
    co_return *p;
  }) > Results;
  co_await fg_Move(Join);
  {
    int Late = 2;
    p = &Late;
  }
  co_return 0;
}
}

// Many calls joined in the same scope are looked at in linear time.
namespace NRound118ManyCalls {
#define REPEAT(x) x x x x x x x x

DMibRunsToCompletion TCFuture<int> fg_Read(int&);

DMibRunsToCompletion TCFuture<int> test() {
  TCFutureVector<int> Results;
  int Value = 0;
  auto Join = co_await fg_AsyncDestroy([&]() -> TCFuture<int> {
    co_await fg_AllDone(Results);
    co_return 0;
  });
  REPEAT(REPEAT(REPEAT(REPEAT(fg_Read(Value) > Results;))))
  co_await fg_Move(Join);
  co_return 0;
}
}

// Round 119: a case label in a branch a constant condition never takes is
// reached from the switch.
namespace NRound119CaseInDeadBranch {
DMibRunsToCompletion TCFuture<int> fg_Read(int &_Value) { co_await CAwaiter{}; co_return _Value; }
DMibRunsToCompletion TCFuture<int> fg_Caller(int _Choice) {
  TCFutureVector<int> Results;
  int Local = 1;
  auto Guard = co_await fg_AsyncDestroy([&]() -> TCFuture<int> {
    co_await fg_AllDone(Results);
    co_return 0;
  });
  fg_Read(Local) > Results;
// CHECK-MESSAGES: :[[@LINE-1]]:11: warning: 'fg_Read' runs to completion and is joined by 'Guard', but this argument may not live until then; pass a local declared before 'Guard' [malterlib-runs-to-completion]
  switch (_Choice) {
    if (false) {
    case 1:
      co_return 0;
    }
  }
  co_await fg_Move(Guard);
  co_return 0;
}
}

// Round 121: a phase guard the cleanup awaits before its join can end it
// first, and so can a registration a factory returns.
namespace NRound121
{
DMibRunsToCompletion TCFuture<int> fg_Read(int &_Value);

DMibRunsToCompletion TCFuture<int> fg_Guarded(CPhaseActor *_pActor) {
  TCFutureVector<int> Results;
  int Local = 42;
  auto Guard = co_await fg_AsyncDestroy([&]() -> TCFuture<int> {
    co_await _pActor->f_CheckDestroyedOnResume();
    co_await fg_AllDone(Results);
    co_return 0;
  });
  fg_Read(Local) > Results;
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: 'fg_Read' runs to completion, but isn't joined here, so it can run after what it refers to is gone; co_await it, or collect its future into a local that an fg_AsyncDestroy guard declared after the local awaits [malterlib-runs-to-completion]
  co_await fg_Move(Guard);
  co_return 0;
}

bool fg_IsCancelled();

auto fg_Watch() {
  return fg_OnResume([]() -> CExceptionPointer {
    return fg_IsCancelled() ? fg_MakeError("Cancelled") : CExceptionPointer{};
  });
}

DMibRunsToCompletion TCFuture<int> fg_Factory() {
  auto OnResume = co_await fg_Watch();
  TCFutureVector<int> Results;
  int Local = 42;
  auto Guard = co_await fg_AsyncDestroy([&]() -> TCFuture<int> {
    co_await fg_AllDone(Results);
    co_return 0;
  });
  fg_Read(Local) > Results;
// CHECK-MESSAGES: :[[@LINE-1]]:11: warning: 'fg_Read' runs to completion and is joined by 'Guard', but this argument may not live until then; pass a local declared before 'Guard' [malterlib-runs-to-completion]
  co_await CAwaiter{};
  co_await fg_Move(Guard);
  co_return 0;
}
}

// Round 122: a switch on the left of a comma runs first.
namespace NRound122Comma
{
DMibRunsToCompletionEntry TCFuture<int> fg_Entry() {
  co_return (co_await fg_ContinueRunningOnActor(fg_ConcurrentActor()), 1);
}
}

// Round 123: either registration of a conditional that is awaited can abort the
// coroutine where it resumes.
namespace NRound123
{
DMibRunsToCompletion TCFuture<int> fg_Read(int &_Value);

DMibRunsToCompletion TCFuture<int> fg_Conditional(bool _bChoose) {
  int Local = 42;
  TCFutureVector<int> Results;
  auto Guard = co_await fg_AsyncDestroy([&]() -> TCFuture<int> {
    co_await fg_AllDone(Results).f_Wrap();
    co_return 0;
  });
  auto fCancel = []() -> CExceptionPointer { return fg_MakeError("Cancelled"); };
  auto OnResume = co_await (_bChoose ? fg_OnResume(fCancel) : fg_OnResume(fCancel));
  fg_Read(Local) > Results;
// CHECK-MESSAGES: :[[@LINE-1]]:11: warning: 'fg_Read' runs to completion and is joined by 'Guard', but this argument may not live until then; pass a local declared before 'Guard' [malterlib-runs-to-completion]
  co_await fg_Work(0).f_Wrap();
  co_await fg_Move(Guard);
  co_return 0;
}
}

// Round 125: `.f_Move()` of a future is the same future.
namespace NRound125
{
DMibRunsToCompletion TCFuture<int> fg_Read(int &_Value);

DMibRunsToCompletion TCFuture<int> fg_Caller(int &_Value) {
  co_return co_await fg_Read(_Value).f_Move();
}
}

// Round 126: a handle changed through a const reference whose const is cast
// away may be any actor.
namespace NRound126
{
DMibRunsToCompletion TCFuture<int> fg_Run(TCActor<> _Other) {
  auto Actor = fg_ConcurrentActor();
  auto const &Alias = Actor;
  const_cast<decltype(Actor) &>(Alias) = _Other;
  co_await fg_ContinueRunningOnActor(Actor);
// CHECK-MESSAGES: :[[@LINE-1]]:12: warning: code that runs to completion switches here to an actor that may be destroyed while it runs, which would abort it; stay on concurrent actors, and call other actors instead [malterlib-runs-to-completion]
  co_return 0;
}
}

// Round 127: a reference into what a pointer parameter of code that runs to
// completion points to lives until the join, and so does either of two
// references that do.
namespace NRound127
{
struct CState {
  int m_Value;
};

DMibRunsToCompletion TCFuture<int> fg_Member(CState *_pState) {
  co_return co_await fg_CallSafe([&Value = _pState->m_Value]() -> TCFuture<int> {
    co_await fg_Work(0);
    co_return Value;
  });
}

DMibRunsToCompletion TCFuture<int> fg_Either(bool _bChoose, int &_A, int &_B) {
  co_return co_await fg_CallSafe([&Value = _bChoose ? _A : _B]() -> TCFuture<int> {
    co_await fg_Work(0);
    co_return Value;
  });
}
}

// Round 128: a registration through any number of functions that return it
// can abort, a default argument is what it is made of, a chain of
// conditionals of references is decided once for each, and a reference to a
// pointer followed twice is decided by the first.
namespace NRound128
{
TCFuture<int> fg_Work(int _Value);
DMibRunsToCompletion TCFuture<int> fg_Read(int &_Value);

auto fg_Watch0() {
  return fg_OnResume([]() -> CExceptionPointer { return fg_MakeError("Cancelled"); });
}
auto fg_Watch1() { return fg_Watch0(); }
auto fg_Watch2() { return fg_Watch1(); }
auto fg_Watch3() { return fg_Watch2(); }
auto fg_Watch4() { return fg_Watch3(); }

DMibRunsToCompletion TCFuture<int> fg_Deep() {
  auto OnResume = co_await fg_Watch4();
  TCFutureVector<int> Results;
  int Local = 42;
  auto Guard = co_await fg_AsyncDestroy([&]() -> TCFuture<int> {
    co_await fg_AllDone(Results).f_Wrap();
    co_return 0;
  });
  fg_Read(Local) > Results;
// CHECK-MESSAGES: :[[@LINE-1]]:11: warning: 'fg_Read' runs to completion and is joined by 'Guard', but this argument may not live until then; pass a local declared before 'Guard' [malterlib-runs-to-completion]
  co_await fg_Work(0).f_Wrap();
  co_await fg_Move(Guard);
  co_return 0;
}

int const g_Global = 42;
DMibRunsToCompletion TCFuture<int> fg_ReadDefault(int const &_Value = g_Global);

DMibRunsToCompletion TCFuture<int> fg_Default() {
  TCFutureVector<int> Results;
  auto Guard = co_await fg_AsyncDestroy([&]() -> TCFuture<int> {
    co_await fg_AllDone(Results).f_Wrap();
    co_return 0;
  });
  fg_ReadDefault() > Results;
  co_return 0;
}

#define DRound128Ref(d_New, d_Old) int &r##d_New = _bChoose ? r##d_Old : r##d_Old;

DMibRunsToCompletion TCFuture<int> fg_Chain(bool _bChoose) {
  int Value = 0;
  int &r0 = Value;
  DRound128Ref(1, 0) DRound128Ref(2, 1) DRound128Ref(3, 2) DRound128Ref(4, 3)
  DRound128Ref(5, 4) DRound128Ref(6, 5) DRound128Ref(7, 6) DRound128Ref(8, 7)
  DRound128Ref(9, 8) DRound128Ref(10, 9) DRound128Ref(11, 10)
  DRound128Ref(12, 11) DRound128Ref(13, 12) DRound128Ref(14, 13)
  DRound128Ref(15, 14) DRound128Ref(16, 15) DRound128Ref(17, 16)
  DRound128Ref(18, 17) DRound128Ref(19, 18) DRound128Ref(20, 19)
  DRound128Ref(21, 20) DRound128Ref(22, 21) DRound128Ref(23, 22)
  DRound128Ref(24, 23) DRound128Ref(25, 24) DRound128Ref(26, 25)
  DRound128Ref(27, 26) DRound128Ref(28, 27) DRound128Ref(29, 28)
  DRound128Ref(30, 29) DRound128Ref(31, 30) DRound128Ref(32, 31)
  co_await (g_Dispatch / [&r32]() -> TCFuture<int> {
    co_await fg_Work(0);
    co_return r32;
  });
  co_return 0;
}

DMibRunsToCompletion TCFuture<int> fg_Alias(bool _bChoose) {
  int Value = 0;
  int *pValue = &Value;
  int *&pAlias = pValue;
  int *pChosen = _bChoose ? pAlias : pAlias;
  co_await (g_Dispatch / [&pChosen]() -> TCFuture<int> {
    co_await fg_Work(0);
    co_return *pChosen;
  });
  co_return 0;
}
}

// Round 129: an early await in the same full expression as the call can run
// before it, and a function that returns a registration on any path can
// return one.
namespace NRound129
{
DMibRunsToCompletion TCFuture<int> fg_Read(int const &_Value);
TCFuture<int> fg_Work();

DMibRunsToCompletion TCFuture<int> fg_SameExpression() {
  TCFutureVector<int> Results;
  int Local = 42;
  auto Guard = co_await fg_AsyncDestroy([&]() -> TCFuture<int> {
    co_await fg_AllDone(Results).f_Wrap();
    co_return 0;
  });
  int Slots[1];
  Slots[(fg_Read(Local) > Results, 0)] = (co_await fg_Move(Guard), 0);
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: 'fg_Read' runs to completion, but isn't joined here, so it can run after what it refers to is gone; co_await it, or collect its future into a local that an fg_AsyncDestroy guard declared after the local awaits [malterlib-runs-to-completion]
  co_return 0;
}

using CWatch = decltype(fg_OnResume([]() -> CExceptionPointer { return {}; }));
CWatch fg_OtherWatch();

CWatch fg_Watch(bool _bChoose) {
  if (_bChoose)
    return fg_OnResume([]() -> CExceptionPointer { return fg_MakeError("Cancelled"); });
  return fg_OtherWatch();
}

DMibRunsToCompletion TCFuture<int> fg_Mixed(bool _bChoose) {
  auto OnResume = co_await fg_Watch(_bChoose);
  TCFutureVector<int> Results;
  int Local = 42;
  auto Guard = co_await fg_AsyncDestroy([&]() -> TCFuture<int> {
    co_await fg_AllDone(Results).f_Wrap();
    co_return 0;
  });
  fg_Read(Local) > Results;
// CHECK-MESSAGES: :[[@LINE-1]]:11: warning: 'fg_Read' runs to completion and is joined by 'Guard', but this argument may not live until then; pass a local declared before 'Guard' [malterlib-runs-to-completion]
  co_await fg_Work().f_Wrap();
  co_await fg_Move(Guard);
  co_return 0;
}
}

// Round 131: an awaiter of a registration forwarded is the registration, and
// what a factory that calls itself back returns isn't kept from before it is
// decided.
namespace NRound131
{
bool fg_Cancelled();
struct CSuspend {
  bool await_ready() noexcept { return false; }
  void await_suspend(std::coroutine_handle<>) noexcept;
  int await_resume() noexcept;
};
DMibRunsToCompletion TCFuture<int> fg_Read(int &_Value);

DMibRunsToCompletion TCFuture<int> fg_Forwarded() {
  auto Awaiter = fg_OnResume([]() -> CExceptionPointer {
    return fg_Cancelled() ? fg_MakeError("Cancelled") : CExceptionPointer{};
  });
  auto Registration = co_await fg_Forward<decltype(Awaiter)>(Awaiter);
  TCFutureVector<int> Results;
  int Local = 0;
  auto Join = co_await fg_AsyncDestroy([&]() -> TCFuture<int> {
    co_await fg_AllDone(Results);
    co_return 0;
  });
  fg_Read(Local) > Results;
// CHECK-MESSAGES: :[[@LINE-1]]:11: warning: 'fg_Read' runs to completion and is joined by 'Join', but this argument may not live until then; pass a local declared before 'Join' [malterlib-runs-to-completion]
  co_await CSuspend{};
  co_await fg_Move(Join);
  co_return 0;
}

using CWatch = decltype(fg_OnResume([]() -> CExceptionPointer { return {}; }));

CWatch fg_WatchA(bool _bChoose);
CWatch fg_WatchB() { return fg_WatchA(false); }
CWatch fg_WatchA(bool _bChoose) {
  if (_bChoose)
    return fg_WatchB();
  return fg_OnResume([]() -> CExceptionPointer {
    return fg_Cancelled() ? fg_MakeError("Cancelled") : CExceptionPointer{};
  });
}

DMibRunsToCompletion TCFuture<int> fg_Prime() {
  auto Registration = co_await fg_WatchA(false);
  TCFutureVector<int> Results;
  int Local = 0;
  auto Join = co_await fg_AsyncDestroy([&]() -> TCFuture<int> {
    co_await fg_AllDone(Results);
    co_return 0;
  });
  fg_Read(Local) > Results;
// CHECK-MESSAGES: :[[@LINE-1]]:11: warning: 'fg_Read' runs to completion and is joined by 'Join', but this argument may not live until then; pass a local declared before 'Join' [malterlib-runs-to-completion]
  co_await CSuspend{};
  co_await fg_Move(Join);
  co_return 0;
}

DMibRunsToCompletion TCFuture<int> fg_Second() {
  auto Registration = co_await fg_WatchB();
  TCFutureVector<int> Results;
  int Local = 0;
  auto Join = co_await fg_AsyncDestroy([&]() -> TCFuture<int> {
    co_await fg_AllDone(Results);
    co_return 0;
  });
  fg_Read(Local) > Results;
// CHECK-MESSAGES: :[[@LINE-1]]:11: warning: 'fg_Read' runs to completion and is joined by 'Join', but this argument may not live until then; pass a local declared before 'Join' [malterlib-runs-to-completion]
  co_await CSuspend{};
  co_await fg_Move(Join);
  co_return 0;
}
}

// Round 132: forwarding the object with flags that capture exceptions,
// `fg_MoveThis<ECoroutineFlag_CaptureExceptions>(*this)`, captures them.
namespace NMib::NConcurrency
{
template <typename t_CType, ECoroutineFlag t_Flags>
struct TCFutureForwardThis : CSuspendNever {
  t_CType await_resume() const noexcept;
};
template <ECoroutineFlag t_Flags, typename t_CType>
TCFutureForwardThis<t_CType &&, t_Flags> fg_MoveThis(t_CType &_This);
}

namespace NRound132
{
DMibRunsToCompletion TCFuture<int> fg_Work(int &_Value);
void fg_MayThrow();

struct CObject {
  DMibRunsToCompletion TCFuture<int> f_Run() {
    auto This = co_await fg_MoveThis<ECoroutineFlag_CaptureExceptions>(*this);
    TCFutureVector<int> Results;
    auto Join = co_await fg_AsyncDestroy([&]() -> TCFuture<int> {
      co_await fg_AllDone(Results);
      co_return 0;
    });
    int Local = 0;
    fg_Work(Local) > Results;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'fg_Work' runs to completion and is joined by 'Join', but this argument may not live until then; pass a local declared before 'Join' [malterlib-runs-to-completion]
    fg_MayThrow();
    co_await fg_Move(Join);
    co_return 0;
  }
};
}

// Round 136: a reference to a transfer to a concurrent actor is it.
namespace NRound136
{
DMibRunsToCompletion TCFuture<int> fg_Run() {
  auto Transfer = fg_ContinueRunningOnActor(fg_ConcurrentActor());
  auto &Alias = Transfer;
  co_await Alias;
  co_return 0;
}
}

namespace NRound137 {
TCFuture<int> fg_Suspend();

// Either local a conditional reference can be bound to, also the second,
// `bChoose ? pFirst : pSecond`.
DMibRunsToCompletion TCFuture<int> fg_Second(bool _bChoose) {
  int First = 1;
  int *pFirst = &First;
  TCFutureVector<int> Results;
  auto Join = co_await fg_AsyncDestroy([&]() -> TCFuture<int> {
    co_await fg_AllDone(Results).f_Wrap();
    co_return 0;
  });
  {
    int Second = 2;
    int *pSecond = &Second;
    int *&pAlias = _bChoose ? pFirst : pSecond;
    (g_Dispatch / [pAlias]() -> TCFuture<int> {
// CHECK-MESSAGES: :[[@LINE-1]]:20: warning: a TCFuture coroutine lambda captures 'pAlias', which points to 'Second', but is not called directly where 'Second' is, so it can run after 'Second' is gone; capture a value or a shared pointer, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
      co_await fg_Suspend();
      co_return *pAlias;
    }) > Results;
  }
  co_await fg_Move(Join);
  co_return 0;
}
}

namespace NRound140 {
DMibRunsToCompletion TCFuture<int> fg_Work(int &_Value);

// A default argument that is used where it runs, besides in a `decltype`.
int *g_pInput;
void fg_Sink(TCFuture<int> _Future = fg_Work(*g_pInput));
// CHECK-MESSAGES: :[[@LINE-1]]:38: warning: 'fg_Work' runs to completion, but is called here where the code doesn't, so it can be aborted while 'fg_Work' refers to its data; call it from a function that runs to completion (DMibRunsToCompletion), or one that begins to (DMibRunsToCompletionEntry) [malterlib-runs-to-completion]
using CSunk = decltype(fg_Sink());
void fg_Unsafe() {
  int Local = 1;
  g_pInput = &Local;
  fg_Sink();
}

// An await that never runs, `false ? co_await ... : 1`, or whose awaiter
// never suspends, `co_await CSuspendNever{}`, can't leave before the join.
DMibRunsToCompletion TCFuture<int> fg_Never() {
  int Local = 0;
  co_return co_await (fg_Work(Local) % (false ? co_await CAwaiter{} : 1));
}

DMibRunsToCompletion TCFuture<int> fg_Ready() {
  int Local = 0;
  co_return co_await (fg_Work(Local) % (co_await CSuspendNever{}, 1));
}

// But one that can suspend can.
DMibRunsToCompletion TCFuture<int> fg_Suspends() {
  int Local = 0;
  co_return co_await (fg_Work(Local) % (co_await CAwaiter{}, 1));
// CHECK-MESSAGES: :[[@LINE-1]]:23: warning: 'fg_Work' runs to completion, but isn't joined here, so it can run after what it refers to is gone; co_await it, or collect its future into a local that an fg_AsyncDestroy guard declared after the local awaits [malterlib-runs-to-completion]
}

// The default arguments that use the one before, which their uses share,
// are looked at once.
int fg_Pair(int, int);
int fg_Add0(int = 0);
int fg_Add1(int = fg_Add0() + fg_Add0());
int fg_Add2(int = fg_Add1() + fg_Add1());
int fg_Add3(int = fg_Add2() + fg_Add2());
int fg_Add4(int = fg_Add3() + fg_Add3());
int fg_Add5(int = fg_Add4() + fg_Add4());
int fg_Add6(int = fg_Add5() + fg_Add5());
int fg_Add7(int = fg_Add6() + fg_Add6());
int fg_Add8(int = fg_Add7() + fg_Add7());
int fg_Add9(int = fg_Add8() + fg_Add8());
int fg_Add10(int = fg_Add9() + fg_Add9());
int fg_Add11(int = fg_Add10() + fg_Add10());
int fg_Add12(int = fg_Add11() + fg_Add11());
int fg_Add13(int = fg_Add12() + fg_Add12());
int fg_Add14(int = fg_Add13() + fg_Add13());
int fg_Add15(int = fg_Add14() + fg_Add14());
int fg_Add16(int = fg_Add15() + fg_Add15());
int fg_Add17(int = fg_Add16() + fg_Add16());
int fg_Add18(int = fg_Add17() + fg_Add17());
int fg_Add19(int = fg_Add18() + fg_Add18());
int fg_Add20(int = fg_Add19() + fg_Add19());
int fg_Add21(int = fg_Add20() + fg_Add20());
int fg_Add22(int = fg_Add21() + fg_Add21());
int fg_Add23(int = fg_Add22() + fg_Add22());
int fg_Add24(int = fg_Add23() + fg_Add23());
int fg_Add25(int = fg_Add24() + fg_Add24());
int fg_Add26(int = fg_Add25() + fg_Add25());
int fg_Add27(int = fg_Add26() + fg_Add26());
int fg_Add28(int = fg_Add27() + fg_Add27());
int fg_Add29(int = fg_Add28() + fg_Add28());
int fg_Add30(int = fg_Add29() + fg_Add29());
int fg_Add31(int = fg_Add30() + fg_Add30());
int fg_Add32(int = fg_Add31() + fg_Add31());
int fg_Add33(int = fg_Add32() + fg_Add32());
int fg_Add34(int = fg_Add33() + fg_Add33());
int fg_Add35(int = fg_Add34() + fg_Add34());
int fg_Add36(int = fg_Add35() + fg_Add35());
int fg_Add37(int = fg_Add36() + fg_Add36());
int fg_Add38(int = fg_Add37() + fg_Add37());
int fg_Add39(int = fg_Add38() + fg_Add38());
int fg_Add40(int = fg_Add39() + fg_Add39());
int fg_Pair0(int = 0);
int fg_Pair1(int = fg_Pair(fg_Pair0(), fg_Pair0()));
int fg_Pair2(int = fg_Pair(fg_Pair1(), fg_Pair1()));
int fg_Pair3(int = fg_Pair(fg_Pair2(), fg_Pair2()));
int fg_Pair4(int = fg_Pair(fg_Pair3(), fg_Pair3()));
int fg_Pair5(int = fg_Pair(fg_Pair4(), fg_Pair4()));
int fg_Pair6(int = fg_Pair(fg_Pair5(), fg_Pair5()));
int fg_Pair7(int = fg_Pair(fg_Pair6(), fg_Pair6()));
int fg_Pair8(int = fg_Pair(fg_Pair7(), fg_Pair7()));
int fg_Pair9(int = fg_Pair(fg_Pair8(), fg_Pair8()));
int fg_Pair10(int = fg_Pair(fg_Pair9(), fg_Pair9()));
int fg_Pair11(int = fg_Pair(fg_Pair10(), fg_Pair10()));
int fg_Pair12(int = fg_Pair(fg_Pair11(), fg_Pair11()));
int fg_Pair13(int = fg_Pair(fg_Pair12(), fg_Pair12()));
int fg_Pair14(int = fg_Pair(fg_Pair13(), fg_Pair13()));
int fg_Pair15(int = fg_Pair(fg_Pair14(), fg_Pair14()));
int fg_Pair16(int = fg_Pair(fg_Pair15(), fg_Pair15()));
int fg_Pair17(int = fg_Pair(fg_Pair16(), fg_Pair16()));
int fg_Pair18(int = fg_Pair(fg_Pair17(), fg_Pair17()));
int fg_Pair19(int = fg_Pair(fg_Pair18(), fg_Pair18()));
int fg_Pair20(int = fg_Pair(fg_Pair19(), fg_Pair19()));
int fg_Pair21(int = fg_Pair(fg_Pair20(), fg_Pair20()));
int fg_Pair22(int = fg_Pair(fg_Pair21(), fg_Pair21()));
int fg_Pair23(int = fg_Pair(fg_Pair22(), fg_Pair22()));
int fg_Pair24(int = fg_Pair(fg_Pair23(), fg_Pair23()));
int fg_Pair25(int = fg_Pair(fg_Pair24(), fg_Pair24()));
int fg_Pair26(int = fg_Pair(fg_Pair25(), fg_Pair25()));
int fg_Pair27(int = fg_Pair(fg_Pair26(), fg_Pair26()));
int fg_Pair28(int = fg_Pair(fg_Pair27(), fg_Pair27()));
int fg_Pair29(int = fg_Pair(fg_Pair28(), fg_Pair28()));
int fg_Pair30(int = fg_Pair(fg_Pair29(), fg_Pair29()));
int fg_Pair31(int = fg_Pair(fg_Pair30(), fg_Pair30()));
int fg_Pair32(int = fg_Pair(fg_Pair31(), fg_Pair31()));
int fg_Pair33(int = fg_Pair(fg_Pair32(), fg_Pair32()));
int fg_Pair34(int = fg_Pair(fg_Pair33(), fg_Pair33()));
int fg_Pair35(int = fg_Pair(fg_Pair34(), fg_Pair34()));
int fg_Pair36(int = fg_Pair(fg_Pair35(), fg_Pair35()));
int fg_Pair37(int = fg_Pair(fg_Pair36(), fg_Pair36()));
int fg_Pair38(int = fg_Pair(fg_Pair37(), fg_Pair37()));
int fg_Pair39(int = fg_Pair(fg_Pair38(), fg_Pair38()));
int fg_Pair40(int = fg_Pair(fg_Pair39(), fg_Pair39()));

DMibRunsToCompletion void fg_Unjoined() {
  int Local = 0;
  (void)(fg_Work(Local) % fg_Add40());
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: 'fg_Work' runs to completion, but isn't joined here, so it can run after what it refers to is gone; co_await it, or collect its future into a local that an fg_AsyncDestroy guard declared after the local awaits [malterlib-runs-to-completion]
}

DMibRunsToCompletion TCFuture<int> fg_EarlyJoin() {
  TCFutureVector<int> Results;
  auto Guard = co_await fg_AsyncDestroy([&]() -> TCFuture<int> {
    co_await fg_AllDone(Results);
    co_return 0;
  });
  int Local = 0;
  fg_Work(Local) > Results;
  fg_Pair40();
  co_await fg_Move(Guard);
  co_return 0;
}
}

namespace NRound141 {
DMibRunsToCompletion TCFuture<int> fg_Work(int &_Value);

// A call in a branch that never runs, `if constexpr (false)` or `if (false)`.
TCFuture<int> fg_Discarded() {
  int Value = 0;
  if constexpr (false)
    co_await fg_Work(Value);
  co_return Value;
}

TCFuture<int> fg_Never() {
  int Value = 0;
  if (false)
    co_await fg_Work(Value);
  co_return Value;
}

// But not one that runs.
TCFuture<int> fg_Always() {
  int Value = 0;
  if (true)
    co_await fg_Work(Value);
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: 'fg_Work' runs to completion, but is called here where the code doesn't, so it can be aborted while 'fg_Work' refers to its data; call it from a function that runs to completion (DMibRunsToCompletion), or one that begins to (DMibRunsToCompletionEntry) [malterlib-runs-to-completion]
  co_return Value;
}
}

namespace NRound146 {
// A wrapper whose `operator co_await` gives the awaiter of a switch is a switch
// to an actor that can't be told.
struct CWrappedSwitch
{
	TCActor<> m_Target;
	auto operator co_await()
	{
		return fg_ContinueRunningOnActor(m_Target).operator co_await();
	}
};

DMibRunsToCompletion TCFuture<int> fg_Wrapped(TCActor<> _Other)
{
	co_await CWrappedSwitch{_Other};
// CHECK-MESSAGES: :[[@LINE-1]]:2: warning: code that runs to completion switches here to an actor that can't be told, which may be destroyed while it runs and abort it; switch with fg_ContinueRunningOnActor to a concurrent actor [malterlib-runs-to-completion]
	co_return 0;
}
}

namespace NRound151 {
// Factories of awaiters that many paths share, one of which returns a call
// of the last, are each told once.
struct CSuspend
{
	bool await_ready() noexcept
	{
		return false;
	}
	void await_suspend(std::coroutine_handle<> _Handle) noexcept;
	int await_resume() noexcept;
};

CSuspend fg_F28(bool _bAgain);
CSuspend fg_F0(bool _bAgain)
{
	if (_bAgain)
		return fg_F28(false);
	return {};
}
#define DRound151F(N, P) \
	CSuspend fg_F##N(bool _bAgain) { return _bAgain ? fg_F##P(false) : fg_F##P(false); }
DRound151F(1, 0) DRound151F(2, 1) DRound151F(3, 2) DRound151F(4, 3) DRound151F(5, 4) DRound151F(6, 5)
DRound151F(7, 6) DRound151F(8, 7) DRound151F(9, 8) DRound151F(10, 9) DRound151F(11, 10) DRound151F(12, 11)
DRound151F(13, 12) DRound151F(14, 13) DRound151F(15, 14) DRound151F(16, 15) DRound151F(17, 16) DRound151F(18, 17)
DRound151F(19, 18) DRound151F(20, 19) DRound151F(21, 20) DRound151F(22, 21) DRound151F(23, 22) DRound151F(24, 23)
DRound151F(25, 24) DRound151F(26, 25) DRound151F(27, 26) DRound151F(28, 27)

DMibRunsToCompletion TCFuture<int> fg_Read(int &_Value);

DMibRunsToCompletion TCFuture<int> fg_Run()
{
	int Value = 0;
	TCFutureVector<int> Results;
	auto Guard = co_await fg_AsyncDestroy
		(
			[&]() -> TCFuture<int>
			{
				co_await fg_AllDone(Results);
				co_return 0;
			}
		)
	;
	fg_Read(Value) > Results;
	co_await fg_F28(false);
	co_await fg_Move(Guard);
	co_return 0;
}
}

namespace NRound157 {
// A reference initialized from itself in an arm a constant condition never
// takes.
DMibRunsToCompletion TCFuture<int> fg_SelfReference() {
  int Value = 1;
  int &Alias = false ? Alias : Value;
  co_return co_await fg_CallSafe([&Alias]() -> TCFuture<int> {
    co_await CAwaiter{};
    co_return Alias;
  });
}
}

namespace NRound159 {
// A switch that is never awaited where it can run.
DMibRunsToCompletion TCFuture<int> fg_Discarded(TCActor<CActor> _Other) {
  auto Switch = fg_ContinueRunningOnActor(_Other);
  if constexpr (false)
    co_await NMib::fg_Move(Switch);
  co_return 1;
}
DMibRunsToCompletion TCFuture<int> fg_Void(TCActor<CActor> _Other) {
  auto Switch = fg_ContinueRunningOnActor(_Other);
  (void)Switch;
  co_return 1;
}
}

namespace NRound162 {
// Exception capture through the awaiter the context gives for it.
struct CContext {
  CCaptureExceptionScopeAwaiter await_transform(CCaptureExceptionsHelper) noexcept;
};
struct CValue {
  ~CValue();
  int m_Value;
};
DMibRunsToCompletion TCFuture<int> fg_Borrow(CValue &_Value);
void fg_Throws();
DMibRunsToCompletion TCFuture<int> fg_Awaiter(CContext &_Coroutine) {
  auto Capture = co_await _Coroutine.await_transform(g_CaptureExceptions);
  CValue Value;
  TCFutureVector<int> Results;
  auto Join = co_await fg_AsyncDestroy([&Results]() -> TCFuture<int> {
    co_await fg_AllDone(Results);
    co_return 0;
  });
  fg_Borrow(Value) > Results;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'fg_Borrow' runs to completion and is joined by 'Join', but this argument may not live until then; pass a local declared before 'Join' [malterlib-runs-to-completion]
  fg_Throws();
  co_await fg_Move(Join);
  co_return 0;
}
// A capture of exceptions in a loop that never runs.
DMibRunsToCompletion TCFuture<int> fg_Read(int const &_Value);
DMibRunsToCompletion TCFuture<int> fg_NeverRuns() {
  while (false) {
    co_await ECoroutineFlag_CaptureExceptions;
  }
  TCFutureVector<int> Results;
  int Local = 1;
  auto Join = co_await fg_AsyncDestroy([&]() -> TCFuture<int> {
    co_await fg_AllDone(Results);
    co_return 0;
  });
  fg_Read(Local) > Results;
  co_await fg_Move(Join);
  co_return 0;
}
}

namespace NRound172 {
TCFuture<int> fg_Pause();
DMibRunsToCompletion TCFuture<int> fg_Work(int &_Value) {
  co_await fg_Pause();
  co_return _Value;
}
// In an instantiation, whose call shares its callee with the template.
template <class t_CType> DMibRunsToCompletion TCFuture<int> fg_Start(int _Value) {
  fg_Work(_Value);
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: 'fg_Work' runs to completion, but isn't joined here, so it can run after what it refers to is gone; co_await it, or collect its future into a local that an fg_AsyncDestroy guard declared after the local awaits [malterlib-runs-to-completion]
  co_return 0;
}
DMibRunsToCompletionEntry TCFuture<int> fg_Caller() {
  co_await fg_ContinueRunningOnActor(fg_ConcurrentActor());
  co_return co_await fg_Start<int>(42);
}
// Not replaced in a branch that is never taken.
DMibRunsToCompletion TCFuture<int> fg_Read(int &_Value);
DMibRunsToCompletion TCFuture<int> fg_NeverTaken() {
  TCFutureVector<int> Results;
  auto Join = co_await fg_AsyncDestroy([&]() -> TCFuture<int> {
    co_await fg_AllDone(Results);
    co_return 0;
  });
  if (false)
    Results = {};
  int Value = 42;
  fg_Read(Value) > Results;
  co_await fg_Move(Join);
  co_return 0;
}
// A lambda called by naming its call operator.
DMibRunsToCompletionEntry TCFuture<int> fg_Named() {
  co_await fg_ContinueRunningOnActor(fg_ConcurrentActor());
  int Value = 42;
  auto fRun = []<class t_CType>(t_CType &_Value) -> TCFuture<int> {
    co_return co_await fg_Work(_Value);
  };
  co_return co_await fRun.operator()<int>(Value);
}
}

namespace NRound173 {
// Run by `fg_CallSafeDispatchedOn` on a concurrent actor and joined.
template <class tf_FFunction>
TCFuture<int> fg_CallSafeDispatchedOn(TCActor<CActor> &&_Actor,
                                      tf_FFunction &&_fFunction);
TCFuture<int> fg_Work();
DMibRunsToCompletion TCFuture<int> fg_Run(int &_Value) {
  co_return co_await fg_CallSafeDispatchedOn(
      fg_ThisConcurrentActor(), [&_Value]() -> TCFuture<int> {
        co_await fg_Work();
        co_return _Value;
      });
}
}

namespace NRound178 {
DMibRunsToCompletion TCFuture<int> fg_Read(int &_Value);
// Called through many parentheses.
DMibRunsToCompletion TCFuture<int> fg_Run(int &_Value) {
  co_return co_await (((fg_Read)))(_Value);
}
}

namespace NRound200 {
template <class t_CType>
t_CType const &fg_Const(t_CType const &_Value) {
  return _Value;
}
struct CHolder {
  int m_Value = 1;
  int *m_pValue = nullptr;
  int const &f_Value() const { return m_Value; }
  int const &f_Pointee() const { return *m_pValue; }
};
DMibRunsToCompletion TCFuture<int> fg_Read(int const &_Value);
// What a call that gives back what it is given refers to, and a member that
// a getter gives, which an early join outlives.
DMibRunsToCompletion TCFuture<int> fg_Run() {
  TCFutureVector<int> Results;
  auto Join = co_await fg_AsyncDestroy([&]() -> TCFuture<int> {
    co_await fg_AllDone(Results);
    co_return 0;
  });
  int Value = 1;
  CHolder Holder;
  fg_Read(fg_Const(Value)) > Results;
  fg_Read(Holder.f_Value()) > Results;
  fg_Read(Holder.f_Pointee()) > Results;
// CHECK-MESSAGES: :[[@LINE-1]]:11: warning: 'fg_Read' runs to completion and is joined by 'Join', but this argument may not live until then; pass a local declared before 'Join' [malterlib-runs-to-completion]
  co_await fg_Move(Join);
  co_return 0;
}
// Without the join, also what they refer to must outlive the call.
DMibRunsToCompletion TCFuture<int> fg_RunUnjoined() {
  TCFutureVector<int> Results;
  {
    int Value = 1;
    CHolder Holder;
    fg_Read(fg_Const(Value)) > Results;
// CHECK-MESSAGES: :[[@LINE-1]]:5: warning: 'fg_Read' runs to completion, but isn't joined here, so it can run after what it refers to is gone; co_await it, or collect its future into a local that an fg_AsyncDestroy guard declared after the local awaits [malterlib-runs-to-completion]
    fg_Read(Holder.f_Value()) > Results;
// CHECK-MESSAGES: :[[@LINE-1]]:5: warning: 'fg_Read' runs to completion, but isn't joined here, so it can run after what it refers to is gone; co_await it, or collect its future into a local that an fg_AsyncDestroy guard declared after the local awaits [malterlib-runs-to-completion]
  }
  co_await fg_AllDone(Results);
  co_return 0;
}
}

namespace NRound204 {
DMibRunsToCompletion TCFuture<int> fg_Work();
// Switched to either of two concurrent actors.
DMibRunsToCompletionEntry TCFuture<int> fg_Run(bool _bLowPriority) {
  co_await fg_ContinueRunningOnActor(_bLowPriority ? fg_ConcurrentActorLowPrio()
                                                   : fg_ConcurrentActor());
  co_return co_await fg_Work();
}
}

namespace NRound207 {
// A local actor made of itself isn't told to be concurrent, and the check
// ends, and a constant condition chooses.
TCFuture<int> fg_Self(bool _bChoose) {
  TCActor<> const &Actor = _bChoose ? Actor : fg_ThisConcurrentActor();
  co_await fg_ContinueRunningOnActor(Actor);
  co_return 0;
}
DMibRunsToCompletionEntry TCFuture<int> fg_Constant(TCActor<> _Other) {
  constexpr bool c_bConcurrent = true;
  co_await fg_ContinueRunningOnActor(c_bConcurrent ? fg_ConcurrentActor()
                                                   : _Other);
  co_return 0;
}
}

namespace NRound209 {
// The actor a constant comparison chooses.
enum EMode { EMode_Concurrent, EMode_Actor };
template <EMode t_Mode>
DMibRunsToCompletionEntry TCFuture<int> fg_Run(TCActor<> _Other) {
  co_await fg_ContinueRunningOnActor(
      t_Mode == EMode_Concurrent ? fg_ConcurrentActor() : _Other);
  co_return 0;
}
void fg_Use(TCActor<> _Other) { (void)fg_Run<EMode_Concurrent>(_Other); }
}

namespace NRound209 {
DMibRunsToCompletion TCFuture<int> fg_Update(int &_Value);
// A future awaited in the cleanup before its join, also one that is ready,
// whose error aborts the cleanup before it joins.
DMibRunsToCompletion TCFuture<int> fg_ReadyBeforeJoin(int &_Value) {
  TCFutureVector<int> Results;
  auto Join = co_await fg_AsyncDestroy([&]() -> TCFuture<int> {
    co_await fg_CallSafe([]() -> TCFuture<int> { co_return 0; });
    co_await fg_AllDone(Results);
    co_return 0;
  });
  fg_Update(_Value) > Results;
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: 'fg_Update' runs to completion, but isn't joined here, so it can run after what it refers to is gone; co_await it, or collect its future into a local that an fg_AsyncDestroy guard declared after the local awaits [malterlib-runs-to-completion]
  co_return 0;
}
DMibRunsToCompletion TCFuture<int> fg_Joined(int &_Value) {
  TCFutureVector<int> Results;
  auto Join = co_await fg_AsyncDestroy([&]() -> TCFuture<int> {
    co_await fg_AllDone(Results);
    co_return 0;
  });
  fg_Update(_Value) > Results;
  co_return 0;
}
}

namespace NRound215 {
DMibRunsToCompletion TCFuture<int> fg_Update(int &_Value);
// A local made before the join that can't leave doesn't skip it.
DMibRunsToCompletion TCFuture<int> fg_Run(int &_Value) {
  TCFutureVector<int> Results;
  auto Join = co_await fg_AsyncDestroy([&]() -> TCFuture<int> {
    int Count = 0;
    co_await fg_AllDone(Results);
    co_return Count;
  });
  fg_Update(_Value) > Results;
  co_return 0;
}
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
	struct TCBase
	{
		virtual TCFuture<int> f_Run(int &o_Value) = 0;
	};
	struct CWorker : TCBase<C24>
	{
		DMibRunsToCompletion TCFuture<int> f_Run(int &o_Value) override
// CHECK-MESSAGES: :[[@LINE-1]]:38: warning: 'f_Run' runs to completion, but overrides 'NRound218::TCBase::f_Run', which doesn't, so the callers that call it through 'NRound218::TCBase::f_Run' aren't checked to join it; declare 'NRound218::TCBase::f_Run' with DMibRunsToCompletion too [malterlib-runs-to-completion]
		{
			co_return o_Value;
		}
	};
}

namespace NRound218Generic
{
	// A call a generic lambda shares with its template, which depends on
	// nothing, is checked in each instantiation: joined there by the await
	// around it, or not joined.
	DMibRunsToCompletion TCFuture<int> fg_CheckCancelled();
	DMibRunsToCompletion TCFuture<int> fg_Joined()
	{
		auto fEach = [&](auto &_Value) -> TCUnsafeFuture<int>
			{
				co_await fg_CheckCancelled();
				co_return _Value;
			}
		;
		int Value = 0;
		co_return co_await fEach(Value);
	}
	DMibRunsToCompletion TCFuture<int> fg_NotJoined()
	{
		auto fEach = [&](auto &_Value) -> TCUnsafeFuture<int>
			{
				fg_CheckCancelled();
// CHECK-MESSAGES: :[[@LINE-1]]:5: warning: 'fg_CheckCancelled' runs to completion, but isn't joined here, so it can run after what it refers to is gone; co_await it, or collect its future into a local that an fg_AsyncDestroy guard declared after the local awaits [malterlib-runs-to-completion]
				co_return _Value;
			}
		;
		int Value = 0;
		co_return co_await fEach(Value);
	}
}

namespace NRound219
{
	// What a pointer captured by reference points into, an element of a
	// vector in a scope that ends before the join, is gone by then.
	template <typename t_CData>
	struct TCVector
	{
		t_CData *m_pData;
		~TCVector();
		void f_SetLen(int _Len);
		t_CData *f_GetArray();
		t_CData &operator [] (int _Index)
		{
			return m_pData[_Index];
		}
	};
	DMibRunsToCompletion TCFuture<int> fg_Element()
	{
		TCFutureVector<int> Results;
		int *pValue = nullptr;
		auto Join = co_await fg_AsyncDestroy
			(
				[&]() -> TCFuture<int>
				{
					co_await fg_AllDone(Results);
					co_return 0;
				}
			)
		;
		{
			TCVector<int> Values;
			Values.f_SetLen(1);
			pValue = &Values[0];
			g_Dispatch / [&pValue]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:19: warning: a TCFuture coroutine lambda captures 'pValue' by reference but is not called directly where 'pValue' is, so it can run after 'pValue' is gone; capture by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
				{
					co_await fg_Work(0);
					co_return *pValue;
				}
				> Results
			;
		}
		co_await fg_Move(Join);
		co_return 0;
	}
	DMibRunsToCompletion TCFuture<int> fg_Array()
	{
		TCFutureVector<int> Results;
		int *pValue = nullptr;
		auto Join = co_await fg_AsyncDestroy
			(
				[&]() -> TCFuture<int>
				{
					co_await fg_AllDone(Results);
					co_return 0;
				}
			)
		;
		{
			TCVector<int> Values;
			Values.f_SetLen(1);
			pValue = Values.f_GetArray();
			g_Dispatch / [&pValue]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:19: warning: a TCFuture coroutine lambda captures 'pValue' by reference but is not called directly where 'pValue' is, so it can run after 'pValue' is gone; capture by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
				{
					co_await fg_Work(0);
					co_return *pValue;
				}
				> Results
			;
		}
		co_await fg_Move(Join);
		co_return 0;
	}
	// Not one that lives until the join.
	DMibRunsToCompletion TCFuture<int> fg_Outer()
	{
		TCFutureVector<int> Results;
		int *pValue = nullptr;
		TCVector<int> Values;
		auto Join = co_await fg_AsyncDestroy
			(
				[&]() -> TCFuture<int>
				{
					co_await fg_AllDone(Results);
					co_return 0;
				}
			)
		;
		Values.f_SetLen(1);
		pValue = &Values[0];
		g_Dispatch / [&pValue]() -> TCFuture<int>
			{
				co_await fg_Work(0);
				co_return *pValue;
			}
			> Results
		;
		co_await fg_Move(Join);
		co_return 0;
	}
}

namespace NRound229
{
	// A registration of fg_OnResume given back by what forwards it is one too.
	template <typename t_CType>
	decltype(auto) fg_ConstOrMove(t_CType &&_In) noexcept
	{
		return fg_Move(_In);
	}

	DMibRunsToCompletion TCFuture<int> fg_Read(int &_Value);
	DMibRunsToCompletion TCFuture<int> fg_Forwarded()
	{
		int Local = 42;
		TCFutureVector<int> Results;
		auto Guard = co_await fg_AsyncDestroy
			(
				[&]() -> TCFuture<int>
				{
					co_await fg_AllDone(Results).f_Wrap();
					co_return 0;
				}
			)
		;
		auto OnResume = co_await fg_ConstOrMove
			(
				fg_OnResume
				(
					[]() -> CExceptionPointer
					{
						return fg_MakeError("Cancelled");
					}
				)
			)
		;
		fg_Read(Local) > Results;
// CHECK-MESSAGES: :[[@LINE-1]]:11: warning: 'fg_Read' runs to completion and is joined by 'Guard', but this argument may not live until then; pass a local declared before 'Guard' [malterlib-runs-to-completion]
		co_await fg_Work(0).f_Wrap();
		co_await fg_Move(Guard);
		co_return 0;
	}
}

namespace NRound231Capture
{
	// What a function gives back of a local it is given by reference doesn't
	// outlive the join when the local doesn't.
	template <class t_CType>
	constexpr t_CType const &fg_Const(t_CType const &_Value) noexcept
	{
		return (t_CType const &)_Value;
	}

	TCFuture<int> fg_Work();

	DMibRunsToCompletion TCFuture<int> fg_Run()
	{
		TCFutureVector<int> Results;
		int const *pValue = nullptr;
		auto Guard = co_await fg_AsyncDestroy
			(
				[&Results]() -> TCFuture<int>
				{
					co_await fg_AllDone(Results).f_Wrap();
					co_return 0;
				}
			)
		;
		{
			int Local = 42;
			pValue = &fg_Const(Local);
			g_Dispatch / [&pValue]() -> TCFuture<int>
// CHECK-MESSAGES: :[[@LINE-1]]:19: warning: a TCFuture coroutine lambda captures 'pValue' by reference but is not called directly where 'pValue' is, so it can run after 'pValue' is gone; capture by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
				{
					co_await fg_Work();
					co_return *pValue;
				}
				> Results
			;
		}
		co_await fg_Move(Guard);
		co_return 0;
	}
}

namespace NRound235
{
	struct CState
	{
		int m_Value;
	};
	template <typename t_CType>
	struct TCOptional
	{
		t_CType m_Value;
		explicit TCOptional(t_CType _Value) : m_Value(_Value) {}
		t_CType &operator * () { return m_Value; }
		t_CType &f_Get() { return m_Value; }
	};
	CState const &fg_Choose(bool _bFirst, CState const &_First, CState const &_Second)
	{
		return _bFirst ? _First : _Second;
	}
	DMibRunsToCompletion TCFuture<int> fg_Read(CState const &_Value);
	// What an operator of a local gives of its member lives as long as it.
	DMibRunsToCompletion TCFuture<int> fg_Optional()
	{
		TCOptional<CState> State(CState{42});
		TCFutureVector<int> Results;
		auto Join = co_await fg_AsyncDestroy
			(
				[&]() -> TCFuture<int>
				{
					co_await fg_AllDone(Results);
					co_return 0;
				}
			)
		;
		fg_Read(*State) > Results;
		fg_Read(State.f_Get()) > Results;
		co_await fg_Move(Join);
		co_return 0;
	}
	// So does either of what a helper returns of what it is given.
	DMibRunsToCompletion TCFuture<int> fg_Chosen(bool _bFirst)
	{
		CState First{1};
		CState Second{2};
		TCFutureVector<int> Results;
		auto Join = co_await fg_AsyncDestroy
			(
				[&]() -> TCFuture<int>
				{
					co_await fg_AllDone(Results);
					co_return 0;
				}
			)
		;
		fg_Read(_bFirst ? First : Second) > Results;
		fg_Read(fg_Choose(_bFirst, First, Second)) > Results;
		co_await fg_Move(Join);
		co_return 0;
	}
	// But not one declared after the join.
	DMibRunsToCompletion TCFuture<int> fg_ChosenLate(bool _bFirst)
	{
		CState First{1};
		TCFutureVector<int> Results;
		auto Join = co_await fg_AsyncDestroy
			(
				[&]() -> TCFuture<int>
				{
					co_await fg_AllDone(Results);
					co_return 0;
				}
			)
		;
		CState Second{2};
		fg_Read(fg_Choose(_bFirst, First, Second)) > Results;
		co_await fg_Move(Join);
		co_return 0;
	}
}

namespace NRound236
{
	void fg_Log(char const *_pMessage) noexcept {}
	DMibRunsToCompletion TCFuture<int> fg_Update(int &_Value);
	// A call before the join, which can only throw, where a throw ends the
	// program, doesn't skip it.
	DMibRunsToCompletion TCFuture<int> fg_Run(int &_Value)
	{
		int Local = 0;
		TCFutureVector<int> Results;
		auto Join = co_await fg_AsyncDestroy
			(
				[&]() -> TCFuture<int>
				{
					fg_Log("Joining work");
					co_await fg_AllDone(Results);
					co_return 0;
				}
			)
		;
		fg_Update(Local) > Results;
		co_await fg_Move(Join);
		co_return 0;
	}
	// But an await before it does.
	DMibRunsToCompletion TCFuture<int> fg_Suspends(int &_Value)
	{
		int Local = 0;
		TCFutureVector<int> Results;
		auto Join = co_await fg_AsyncDestroy
			(
				[&]() -> TCFuture<int>
				{
					co_await fg_Update(_Value);
					co_await fg_AllDone(Results);
					co_return 0;
				}
			)
		;
		fg_Update(Local) > Results;
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: 'fg_Update' runs to completion, but isn't joined here, so it can run after what it refers to is gone; co_await it, or collect its future into a local that an fg_AsyncDestroy guard declared after the local awaits [malterlib-runs-to-completion]
		co_await fg_Move(Join);
		co_return 0;
	}
}
namespace NRound236Captures
{
	void fg_Log(char const *_pMessage);
	DMibRunsToCompletion TCFuture<int> fg_Update(int &_Value);
	// Nor a call in a cleanup that captures exceptions, where it can throw
	// past the join.
	DMibRunsToCompletion TCFuture<int> fg_Run()
	{
		int Local = 0;
		TCFutureVector<int> Results;
		auto Join = co_await fg_AsyncDestroy
			(
				[&]() -> TCFuture<int>
				{
					co_await ECoroutineFlag_CaptureExceptions;
					fg_Log("Joining work");
					co_await fg_AllDone(Results);
					co_return 0;
				}
			)
		;
		fg_Update(Local) > Results;
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: 'fg_Update' runs to completion, but isn't joined here, so it can run after what it refers to is gone; co_await it, or collect its future into a local that an fg_AsyncDestroy guard declared after the local awaits [malterlib-runs-to-completion]
		co_await fg_Move(Join);
		co_return 0;
	}
}
