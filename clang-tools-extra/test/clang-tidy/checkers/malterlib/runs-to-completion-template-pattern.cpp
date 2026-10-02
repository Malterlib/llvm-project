// RUN: %check_clang_tidy -std=c++20-or-later %s malterlib-runs-to-completion %t -- -config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" -- -I %S/Inputs

#include "malterlib-runs-to-completion.h"

// What can run is told in the instantiation a node is in, not in the
// template, of which no control flow is built: a node the instantiation
// shares with its template, the `_f` of a literal in a branch of an `if
// constexpr` the template can't decide, has the template as a parent too.

struct CStr
{
	CStr &operator=(int);
};
int operator""_f(char const *, decltype(sizeof(0)));

struct CA
{
	int m_X;
};

template <typename tf_CClass>
bool fg_IsUpdated(auto &_Wanted, auto(tf_CClass:: *_pMember))
{
	auto &Wanted = _Wanted.*_pMember;
	CStr NewValue;
	if constexpr (requires { Wanted.f_Missing(); })
		;
	else
		NewValue = "{}"_f;
	return true;
}

bool fg_Use(CA &_A)
{
	return fg_IsUpdated<CA>(_A, &CA::m_X);
}

struct CState
{
	int m_Value = 0;
};

DMibRunsToCompletion TCFuture<int> fg_Update(CState &_State);

// The branch the instantiation keeps is checked, the one it discards isn't.
template <typename tf_CClass>
DMibRunsToCompletion TCFuture<int> fg_Updated(auto &_Wanted, auto(tf_CClass:: *_pMember), CState &_State)
{
	auto &Wanted = _Wanted.*_pMember;
	CStr NewValue;
	if constexpr (requires { Wanted.f_Missing(); })
		fg_Update(_State);
	else
	{
		NewValue = "{}"_f;
		fg_Update(_State);
// CHECK-MESSAGES: :[[@LINE-1]]:3: warning: 'fg_Update' runs to completion, but isn't joined here, {{.*}}
	}
	co_return 0;
}

DMibRunsToCompletion TCFuture<int> fg_UseUpdated(CA &_A, CState &_State)
{
	co_return co_await fg_Updated<CA>(_A, &CA::m_X, _State);
}
