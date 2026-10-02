// RUN: %check_clang_tidy -std=c++20-or-later %s malterlib-actor-dispatch-this,malterlib-coroutine-unsafe-this,malterlib-continue-running-on-actor %t -- -config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" -- -I %S/Inputs

// Where the actor checks narrow a declaration, an expression or a type to a
// kind, the kinds that look like it and aren't: a member named through a
// local can be a static data member, which is no part of the local.

#include "malterlib-concurrency.h"

struct CItems
{
	int m_Value;
};

struct CMixedView
{
	int m_Value;
	CItems *m_pItems;
	static int *ms_pShared;
	static int ms_Shared;
};

struct CApp : CActor
{
	CItems mp_Items;
	TCActor<> mp_Other;

	TCFuture<int> f_StaticMember()
	{
		CMixedView Source{0, &mp_Items};
		co_return co_await (g_Dispatch(mp_Other) / [pUse = Source.ms_pShared] { return *pUse; });
	}
	TCFuture<int> f_StaticMemberAddress()
	{
		CMixedView Source{0, &mp_Items};
		co_return co_await (g_Dispatch(mp_Other) / [pUse = &Source.ms_Shared] { return *pUse; });
	}
	TCFuture<int> f_BorrowedMember()
	{
		CMixedView Source{0, &mp_Items};
		co_return co_await (g_Dispatch(mp_Other) / [pUse = Source.m_pItems] { return pUse->m_Value; });
// CHECK-MESSAGES: :[[@LINE-1]]:47: warning: a lambda that captures 'this' of actor 'CApp' runs on another actor, {{.*}}
	}
};
