// RUN: %check_clang_tidy -std=c++20-or-later %s malterlib-actor-phase %t -- -config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" -- -I %S/Inputs -fms-extensions -fsigned-char

// Microsoft's sized literals as constant indices: `0i128` is 128 bits wide
// and `0` 32, and `0xFFi8` is a signed char, -1. Two indices name the same
// element when their numbers are equal.

#include "malterlib-actor-phase.h"

struct CIndexedState
{
	DMibActorEntries(Owner) TCMap<CStr, int> m_Items;
};

struct CApp : CDistributedAppActor
{
	CIndexedState *m_pStates;

	void f_WideFirst()
	{
		auto Key = fg_RandomID(m_pStates[0i128].m_Items);
		m_pStates[0i128].m_Items[Key];
		m_pStates[0].m_Items.f_Remove(Key);
	}

	void f_WideSecond()
	{
		auto Key = fg_RandomID(m_pStates[0].m_Items);
		m_pStates[0].m_Items[Key];
		m_pStates[0i128].m_Items.f_Remove(Key);
	}

	void f_SameNegative()
	{
		auto Key = fg_RandomID(m_pStates[0xFFFFi16].m_Items);
		m_pStates[0xFFFFi16].m_Items[Key];
		m_pStates[0xFFFFFFFFi32].m_Items.f_Remove(Key);
		auto Key1 = fg_RandomID(m_pStates[1i64].m_Items);
		m_pStates[1i64].m_Items[Key1];
		m_pStates[1i8].m_Items.f_Remove(Key1);
	}

	void f_NegativeIsNotUnsigned()
	{
		auto Key = fg_RandomID(m_pStates[0xFFi8].m_Items);
		m_pStates[0xFFi8].m_Items[Key];
		m_pStates[255].m_Items.f_Remove(Key);
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: 'm_Items' is declared to lose entries only by the calls that added them, {{.*}}
	}
};
