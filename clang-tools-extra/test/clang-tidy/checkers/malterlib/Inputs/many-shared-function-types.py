# Writes a function type nested to the given depth, each level of which names
# the one before three times, and a field of a template instantiated with it.
# With "parents", also code whose checks build the parent map, a modulo the
# matcher of malterlib-coawait-precedence looks at the parents of, and the
# instantiation index, of a coroutine template instantiated. With "aliases",
# also a use of an alias template, which the naming checks collect the uses
# of. With "arguments", instead a type list nested to the given depth, each
# level of which names the one before ten times, that a partial
# specialization matches, which the naming checks compare with what is
# written. With "coroutines", instead coroutine templates instantiated with
# the function type, whose locals, captures and awaits the coroutine checks
# collect. With "defaults", instead a class template whose default argument
# is a function type with a named parameter of the template parameter, which
# the naming checks collect the function types of. With "callbacks", also
# a function template whose default argument is a function type with a
# parameter of an alias template nested to the given depth, each level of
# which names the one before thirty times, which the naming checks make with
# the arguments of each specialization.
import sys

Depth = int(sys.argv[1])
Mode = sys.argv[2] if len(sys.argv) > 2 else ""
if Mode == "arguments":
    print("template <class... tp_CTypes> struct TCTypeList {};")
    print("template <class t_CType> using TCLevel0 = t_CType;")
    for Index in range(1, Depth + 1):
        Arguments = ", ".join([f"TCLevel{Index - 1}<t_CType>"] * 10)
        print(f"template <class t_CType> using TCLevel{Index} = TCTypeList<{Arguments}>;")
    print("template <class t_CType, class t_COther> struct TCTraits {")
    print("  using CType = t_COther;")
    print("};")
    print("template <class t_CType, class t_COther>")
    print(f"struct TCTraits<TCLevel{Depth}<t_CType>, t_COther> {{")
    print("  using CType = t_COther;")
    print("};")
    print("template <class tf_CType>")
    print(f"void fg_Test(typename TCTraits<TCLevel{Depth}<tf_CType>, tf_CType>::CType _Value) {{}}")
    sys.exit(0)
if Mode == "defaults":
    print("template <typename t_CSignature> struct TCFunction;")
    print("template <typename t_CReturn, typename... tp_CParams> struct TCFunction<t_CReturn(tp_CParams...)> {};")
    print("using F0 = int;")
    for Index in range(1, Depth + 1):
        print(f"using F{Index} = F{Index - 1} (*)(F{Index - 1} _fLeft, F{Index - 1} _fRight);")
    print("template <typename t_CType, typename t_FCallback = TCFunction<void(t_CType o_Value)>>")
    print("struct TCHolder {};")
    print(f"TCHolder<F{Depth}> g_Holder;")
    sys.exit(0)
if Mode == "callbacks":
    print("template <typename t_FSignature> struct TCFunction {};")
    print("struct CTraits { using CBool = bool; };")
    print("template <typename t_CTraits> using TF0 = typename t_CTraits::CBool;")
    for Index in range(1, Depth + 1):
        Parameters = ", ".join([f"TF{Index - 1}<t_CTraits>"] * 30)
        print(f"template <typename t_CTraits> using TF{Index} = TF{Index - 1}<t_CTraits> (*)({Parameters});")
    print("template <typename tf_CTraits,")
    print(f"          typename tf_FCallback = TCFunction<void(typename tf_CTraits::CBool _bReady, TF{Depth}<tf_CTraits> _fCallback)>>")
    print("void fg_Test() {}")
    print("void fg_Use() { fg_Test<CTraits>(); }")
    sys.exit(0)
if Mode == "coroutines":
    print('#include "malterlib-runs-to-completion.h"')
    print("using F0 = int;")
    for Index in range(1, Depth + 1):
        print(f"using F{Index} = F{Index - 1} (*)(F{Index - 1}, F{Index - 1});")
    print(f"""struct CState {{ int m_Value = 0; }};
TCFuture<int> fg_Work(int _Value);

template <typename t_CType>
TCFuture<int> fg_Read(t_CType _Value, CState &_State)
{{
	t_CType Copy = _Value;
	auto Auto = _Value;
	auto fLambda = [&Copy, Auto](t_CType _Param) -> t_CType {{ t_CType Inner = _Param; return Inner; }};
	(void)fLambda;
	int Local = 0;
	auto fRef = [&]() -> TCFuture<int> {{ t_CType In = Copy; co_await CAwaiter{{}}; co_return Local + _State.m_Value; }};
	(void)fRef;
	co_await CAwaiter{{}};
	t_CType After = Copy;
	(void)After;
	co_return _State.m_Value;
}}

struct CMyActor : CActor
{{
	int m_Member = 0;
	template <typename t_CType>
	TCFuture<int> f_Work(t_CType _Value)
	{{
		t_CType Copy = _Value;
		auto Auto = Copy;
		int *pMember = &m_Member;
		co_await fg_ContinueRunningOnActor(TCActor<>());
		t_CType After = Auto;
		(void)After;
		co_return *pMember;
	}}
}};

template <typename t_CType>
DMibRunsToCompletion TCFuture<int> fg_Update(t_CType _Value, CState &_State)
{{
	t_CType Copy = _Value;
	co_await fg_Work(0);
	TCFutureVector<int> Results;
	auto JoinResults = co_await fg_AsyncDestroy
		(
			[&]() -> TCFuture<int>
			{{
				t_CType Inner = Copy;
				(void)Inner;
				co_await fg_AllDone(Results);
				co_return 0;
			}}
		)
	;
	int Local = 0;
	g_Dispatch / [&_State, &Local, Copy]() -> TCFuture<int>
		{{
			t_CType Inner = Copy;
			(void)Inner;
			co_await fg_Work(0);
			co_return _State.m_Value + Local;
		}}
		> Results
	;
	co_return 0;
}}

void fg_Use(CMyActor &_Actor)
{{
	CState State;
	fg_Read(F{Depth}{{}}, State);
	_Actor.f_Work(F{Depth}{{}});
	fg_Update(F{Depth}{{}}, State);
}}""")
    sys.exit(0)
if Mode == "parents":
    print('#include "malterlib-concurrency.h"')
print("template <class t_CType> struct TCBox { t_CType m_Value; };")
print("using F0 = int;")
for Index in range(1, Depth + 1):
    print(f"using F{Index} = F{Index - 1} (*)(F{Index - 1}, F{Index - 1});")
print(f"TCBox<F{Depth}> g_Value;")
if Mode == "parents":
    print("int fg_Modulo() { return 1 % 2; }")
    print("template <class t_CType> TCFuture<int> fg_Read(t_CType) { co_return 0; }")
    print("void fg_Use() { fg_Read(0); }")
if Mode == "aliases":
    print("template <class t_CType> struct TCAsyncResult {};")
    print("template <class t_FSignature> struct TCFunction {};")
    print("template <class t_CReturnValue>")
    print("using TCFutureOnResult = TCFunction<void(TCAsyncResult<t_CReturnValue> &&_AsyncResult)>;")
    print("TCFutureOnResult<int> g_fOnResult;")
