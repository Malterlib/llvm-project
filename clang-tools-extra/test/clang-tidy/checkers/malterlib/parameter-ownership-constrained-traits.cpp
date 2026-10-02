// RUN: %check_clang_tidy -std=c++20-or-later %s malterlib-parameter-ownership %t -- -config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}"

// A partial specialization of the coroutine traits that the translation unit
// doesn't instantiate for a function doesn't make it one that can be a
// coroutine, which takes what it owns by value.
namespace std
{
	template <typename t_CReturn, typename ...tp_CArgs>
	struct coroutine_traits
	{
		using promise_type = typename t_CReturn::promise_type;
	};
	template <typename t_CResult, typename t_CParam>
		requires (sizeof(t_CParam) > 64)
	struct coroutine_traits<t_CResult, t_CParam>
	{
		struct promise_type;
	};
}
struct COwner
{
	int *m_pData;
	~COwner()
	{
		delete m_pData;
	}
};
void fg_Constrained(COwner _Owner)
// CHECK-MESSAGES: :[[@LINE-1]]:28: warning: parameter '_Owner' takes 'COwner' by value; {{.*}}
{
}
void fg_Other(COwner _Owner, int _Count)
{
}
// CHECK-MESSAGES: :[[@LINE-3]]:22: warning: parameter '_Owner' takes 'COwner' by value; {{.*}}

// But the constraints of one for any parameters, which aren't told, are
// taken to accept them, like those of Malterlib's futures.
struct CTask
{
};
namespace std
{
	template <typename ...tp_CArgs>
		requires (sizeof...(tp_CArgs) == 0)
	struct coroutine_traits<CTask, tp_CArgs...>
	{
		struct promise_type
		{
		};
	};
}
CTask fg_Packed(COwner _Owner) { return {}; }
