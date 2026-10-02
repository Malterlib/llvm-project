// RUN: %check_clang_tidy -std=c++20-or-later %s malterlib-coroutine-actor-reference,malterlib-async-destroy,malterlib-continue-running-on-actor %t -- -config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" -- -I %S/Inputs

// An argument given to a C ellipsis has no parameter, and so no parameter
// type: what is given there is read like what is given to a parameter whose
// type can't be told, as the same argument given to a prototyped function.

#include "malterlib-actor-phase.h"

TCFuture<int> fg_Suspend();
void fg_Log(char const *_pFormat, ...);
void fg_LogInt(char const *_pFormat, int _Value);
void fg_LogPointer(char const *_pFormat, int *_pValue);

struct CEntry
{
	int m_Value;
	TCSharedPointer<bool> m_pDeleted;
};

struct CApp : CActor
{
	TCMap<int, CEntry> mp_Items;

	TCFuture<int> f_Variadic()
	{
		auto &Entry = mp_Items[0];
		auto pDeleted = Entry.m_pDeleted;
		co_await fg_Suspend();
		if (*pDeleted)
			co_return 0;
		fg_Log("Value: %d", Entry.m_Value);
		co_return Entry.m_Value;
	}

	TCFuture<int> f_Prototyped()
	{
		auto &Entry = mp_Items[0];
		auto pDeleted = Entry.m_pDeleted;
		co_await fg_Suspend();
		if (*pDeleted)
			co_return 0;
		fg_LogInt("Value: %d", Entry.m_Value);
		co_return Entry.m_Value;
	}

	TCFuture<int> f_VariadicPointer()
	{
		auto &Entry = mp_Items[0];
		auto pDeleted = Entry.m_pDeleted;
		co_await fg_Suspend();
		if (*pDeleted)
			co_return 0;
		fg_Log("Value: %p", &Entry.m_Value);
		co_return Entry.m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'Entry' refers into the data of the actor and is used after a suspension, {{.*}}
	}

	TCFuture<int> f_PrototypedPointer()
	{
		auto &Entry = mp_Items[0];
		auto pDeleted = Entry.m_pDeleted;
		co_await fg_Suspend();
		if (*pDeleted)
			co_return 0;
		fg_LogPointer("Value: %p", &Entry.m_Value);
		co_return Entry.m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'Entry' refers into the data of the actor and is used after a suspension, {{.*}}
	}

	TCFuture<int> f_Unguarded()
	{
		auto &Entry = mp_Items[0];
		co_await fg_Suspend();
		fg_Log("Value: %d", Entry.m_Value);
// CHECK-MESSAGES: :[[@LINE-1]]:23: warning: 'Entry' refers into the data of the actor and is used after a suspension, {{.*}}
		co_return 0;
	}
};
