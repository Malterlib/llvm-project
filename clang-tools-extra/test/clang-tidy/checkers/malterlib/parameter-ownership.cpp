// RUN: %check_clang_tidy -std=c++20-or-later %s malterlib-parameter-ownership %t -- -config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml', malterlib-parameter-ownership.AllowedTypes: '::NAllowed::CAllowed'}}"

namespace std
{
	template <typename t_CType>
	t_CType &&move(t_CType &_Value);

	template <typename t_CPromise = void>
	struct coroutine_handle
	{
		static coroutine_handle from_address(void *);
		void *address() const;
	};

	template <typename t_CReturn, typename ...tp_CArgs>
	struct coroutine_traits
	{
		using promise_type = typename t_CReturn::promise_type;
	};

	struct suspend_never
	{
		bool await_ready() const noexcept { return true; }
		template <typename t_CHandle>
		void await_suspend(t_CHandle) const noexcept {}
		void await_resume() const noexcept {}
	};
}

struct CFuture
{
	struct promise_type
	{
		CFuture get_return_object();
		std::suspend_never initial_suspend();
		std::suspend_never final_suspend() noexcept;
		void return_void();
		void unhandled_exception();
	};
};

struct CStr
{
	CStr();
	CStr(CStr const &);
	CStr(CStr &&);
	~CStr();
};

struct CTrivial
{
	int m_Value;
};

void fg_Borrow(CStr const &_Value);
void fg_Consume(CStr &&_Value);

void fg_ByValue(CStr _Value, CTrivial _Trivial, int _Number)
// CHECK-MESSAGES: :[[@LINE-1]]:22: warning: parameter '_Value' takes 'CStr' by value; take it by 'const &' when the function borrows it or by '&&' when it consumes it [malterlib-parameter-ownership]
{
	fg_Borrow(_Value);
}

void fg_Moved(CStr _Value)
// CHECK-MESSAGES: :[[@LINE-1]]:20: warning: parameter '_Value' takes 'CStr' by value; take it by '&&', since the function consumes it [malterlib-parameter-ownership]
{
	fg_Consume(std::move(_Value));
}

void fg_Unnamed(CStr)
// CHECK-MESSAGES: :[[@LINE-1]]:17: warning: unnamed parameter takes 'CStr' by value; take it by 'const &' when the function borrows it or by '&&' when it consumes it [malterlib-parameter-ownership]
{
}

// Only definitions are checked, since a declaration doesn't tell whether the
// function is a coroutine.
void fg_Declaration(CStr _Value);

// A coroutine takes what it needs by value.
CFuture fg_Coroutine(CStr _Value)
{
	co_return;
}

// So does a function that can be one, by what it returns, like an actor's
// member function that isn't a coroutine yet.
CFuture fg_NotYetCoroutine(CStr _Value)
{
	return {};
}

// An interface can't be a coroutine; its overrides take its signature.
struct ICInterface
{
	virtual void f_Function(CStr _Value) = 0;
// CHECK-MESSAGES: :[[@LINE-1]]:31: warning: parameter '_Value' takes 'CStr' by value; take it by 'const &' when the function borrows it or by '&&' when it consumes it [malterlib-parameter-ownership]
	virtual CFuture f_Coroutine(CStr _Value) = 0;
};

struct CImplementation : ICInterface
{
	void f_Function(CStr _Value) override
	{
	}
};

// A template is checked as it is instantiated, like with `CStr`.
template <typename tf_CType>
void fg_Template(tf_CType _Value)
// CHECK-MESSAGES: :[[@LINE-1]]:27: warning: parameter '_Value' takes 'CStr' by value; {{.*}}
{
}

void fg_Instantiate()
{
	fg_Template(CStr());

	auto fLambda = [](CStr _Value)
// CHECK-MESSAGES: :[[@LINE-1]]:25: warning: parameter '_Value' takes 'CStr' by value; take it by 'const &' when the function borrows it or by '&&' when it consumes it [malterlib-parameter-ownership]
		{
		}
	;
}

template <typename tf_CType>
void fg_Specialized(tf_CType _Value);

// A specialization is what an instantiation of the primary template would be
// for its type, and takes it by value like it: the fix is in the primary.
template <>
void fg_Specialized<CStr>(CStr _Value)
// CHECK-MESSAGES: :[[@LINE-1]]:32: warning: parameter '_Value' takes 'CStr' by value; {{.*}}
{
}

extern "C" void c_function(CStr _Value)
{
}

void fg_CppFunction(CStr _Value)
// CHECK-MESSAGES: :[[@LINE-1]]:26: warning: parameter '_Value' takes 'CStr' by value; {{.*}}
{
}

struct CMapIterator
{
	int *mp_pCurrent;
	~CMapIterator() {}
};

// An iterator doesn't own what it is at.
void fg_Walk(CMapIterator _Iterator)
{
}

namespace NAllowed
{
	struct COwner
	{
		COwner(COwner const &);
		~COwner();
	};
	using CAllowed = COwner;
}

// Allowed through an alias.
void fg_AllowedAlias(NAllowed::CAllowed _Value)
{
}
// The same type, owning, without the alias.
void fg_NotAllowedWithoutAlias(NAllowed::COwner _Value)
// CHECK-MESSAGES: :[[@LINE-1]]:49: warning: parameter '_Value' takes {{.*}}
{
}

// A closure that captures an owning object owns it.
inline auto g_fOwning = [Str = CStr()] {};
void fg_Closure(decltype(g_fOwning) _fClosure)
// CHECK-MESSAGES: :[[@LINE-1]]:37: warning: parameter '_fClosure' takes {{.*}}
{
}

void fg_NoLint(CStr _Value) // NOLINT(malterlib-parameter-ownership)
{
}

// A view copies what it is at, which it doesn't own, even when copying it does
// more than copy its bytes.
struct CView
{
	char const *m_pData;
	CView(CView const &_Other) : m_pData(_Other.m_pData) {}
};
void fg_View(CView _View)
{
}

// An iterator that releases what it holds owns it, and a view with an empty
// destructor doesn't.
struct CReleasingIterator
{
	int *mp_pOwned = nullptr;
	~CReleasingIterator()
	{
		delete mp_pOwned;
	}
};
void fg_ConsumeIterator(CReleasingIterator _Iterator)
// CHECK-MESSAGES: :[[@LINE-1]]:44: warning: parameter '_Iterator' takes 'CReleasingIterator' by value; {{.*}}
{
}
struct CEmptyDestructorView
{
	char const *mp_pData;
	~CEmptyDestructorView()
	{
	}
};
void fg_EmptyDestructorView(CEmptyDestructorView _View)
{
}

// A destructor whose work is in a discarded branch of an `if constexpr` does
// nothing in that instantiation.
template <bool t_bOwns>
struct TCMaybeOwningIterator
{
	int *mp_pCurrent;
	~TCMaybeOwningIterator()
	{
		if constexpr (t_bOwns)
			delete mp_pCurrent;
	}
};
void fg_WalkMaybeOwning(TCMaybeOwningIterator<false> _Iterator)
{
}
void fg_UseMaybeOwning(int *_pCurrent)
{
	fg_WalkMaybeOwning({_pCurrent});
}
template struct TCMaybeOwningIterator<true>;
void fg_ConsumeMaybeOwning(TCMaybeOwningIterator<true> _Iterator)
// CHECK-MESSAGES: :[[@LINE-1]]:56: warning: parameter '_Iterator' takes 'TCMaybeOwningIterator<true>' by value; {{.*}}
{
}

// A destructor of a specialization that isn't instantiated isn't looked into,
// `TCUninstantiatedIterator<false>`, which its instantiation would tell, but
// one that is, explicitly, `template struct TCUninstantiatedIterator<true>`,
// is.
template <bool t_bOwns>
struct TCUninstantiatedIterator
{
	int *mp_pCurrent;
	~TCUninstantiatedIterator()
	{
		if constexpr (t_bOwns)
			delete mp_pCurrent;
	}
};
void fg_WalkUninstantiated(TCUninstantiatedIterator<false> _Iterator)
{
}
template struct TCUninstantiatedIterator<true>;
void fg_ConsumeUninstantiated(TCUninstantiatedIterator<true> _Iterator)
// CHECK-MESSAGES: :[[@LINE-1]]:62: warning: parameter '_Iterator' takes 'TCUninstantiatedIterator<true>' by value; take it by 'const &' when the function borrows it or by '&&' when it consumes it [malterlib-parameter-ownership]
{
}

// A function template whose result depends on its parameters is checked when
// an instantiation can't be a coroutine; a destructor defined elsewhere in a
// class that is copied as is doesn't release anything, and neither does a
// discarded branch.
struct CTemplateOwner
{
	int *m_pData;
	~CTemplateOwner()
	{
		delete m_pData;
	}
};
template <typename tf_CReturn>
tf_CReturn fg_BorrowTemplate(CTemplateOwner _Owner)
// CHECK-MESSAGES: :[[@LINE-1]]:45: warning: parameter '_Owner' takes 'CTemplateOwner' by value; take it by 'const &' when the function borrows it or by '&&' when it consumes it [malterlib-parameter-ownership]
{
	return {};
}
int g_BorrowTemplate = fg_BorrowTemplate<int>(CTemplateOwner{nullptr});
struct CViewDefinedElsewhere
{
	int const *m_pData;
	~CViewDefinedElsewhere();
};
void fg_ViewDefinedElsewhere(CViewDefinedElsewhere _View)
{
}
struct CDiscardedRelease
{
	int *m_pData;
	~CDiscardedRelease()
	{
		if constexpr (false)
			delete m_pData;
	}
};
void fg_DiscardedRelease(CDiscardedRelease _View)
{
}

// A condition on the size of a template parameter.
template <typename t_CType>
struct TCSizedView
{
	int *m_pData;
	~TCSizedView()
	{
		if constexpr (sizeof(t_CType) == 1)
			delete m_pData;
	}
};
void fg_BorrowSized(TCSizedView<int> _View)
{
}

// Conversions and comparisons in the conditions of an uninstantiated
// destructor, and an inherited promise_type.
template <int t_Flags>
struct TCFlagOwner
{
	int *m_pData;
	~TCFlagOwner()
	{
		if constexpr (static_cast<bool>(t_Flags) == true)
			delete m_pData;
	}
};
template struct TCFlagOwner<2>;
void fg_TakeFlagOwner(TCFlagOwner<2> _Owner)
// CHECK-MESSAGES: :[[@LINE-1]]:38: warning: parameter '_Owner' takes 'TCFlagOwner<2>' by value; take it by 'const &' when the function borrows it or by '&&' when it consumes it [malterlib-parameter-ownership]
{
}
template <typename t_CType>
struct TCSmallView
{
	int *m_pData;
	~TCSmallView()
	{
		if constexpr (sizeof(t_CType) < 2)
			delete m_pData;
	}
};
void fg_WalkSmallView(TCSmallView<int> _View)
{
}
struct CInheritedPromise;
struct CInheritedFutureBase
{
	using promise_type = CInheritedPromise;
};
struct CInheritedFuture : CInheritedFutureBase
{
};
CInheritedFuture fg_InheritedPromise(CStr _Str)
{
	return {};
}

// A conversion to a narrower integer, a specialization of the traits for
// another result, and a destructor that only checks.
template <int t_Flags>
struct TCNarrowedOwner
{
	int *m_pData;
	~TCNarrowedOwner()
	{
		if constexpr (static_cast<unsigned char>(t_Flags) == 0)
			delete m_pData;
	}
};
template struct TCNarrowedOwner<256>;
void fg_TakeNarrowedOwner(TCNarrowedOwner<256> _Owner)
// CHECK-MESSAGES: :[[@LINE-1]]:48: warning: parameter '_Owner' takes 'TCNarrowedOwner<256>' by value; take it by 'const &' when the function borrows it or by '&&' when it consumes it [malterlib-parameter-ownership]
{
}
template <typename t_CType>
struct TCTraitsResult
{
};
namespace std
{
	template <typename t_CType, typename ...tp_CArgs>
	struct coroutine_traits<TCTraitsResult<t_CType *>, tp_CArgs...>
	{
		struct promise_type;
	};
}
TCTraitsResult<int> fg_NotForTraits(CStr _Value)
// CHECK-MESSAGES: :[[@LINE-1]]:42: warning: parameter '_Value' takes 'CStr' by value; take it by 'const &' when the function borrows it or by '&&' when it consumes it [malterlib-parameter-ownership]
{
	return {};
}
void fg_FailCheck();
struct CCheckingView
{
	int const *m_pData;
	~CCheckingView()
	{
		if (!m_pData)
			fg_FailCheck();
	}
};
void fg_WalkCheckingView(CCheckingView _View)
{
}

// Casts in a template argument, also 128 bits wide and signed, a member cast
// before it is released, a destructor that only checks its object, and actor
// members, which may take parameters by value.
template <int t_Flags>
struct TCWideView
{
	int *m_pData;
	~TCWideView()
	{
		if constexpr (static_cast<unsigned __int128>(t_Flags) == 0)
			delete m_pData;
	}
};
void fg_WideView(TCWideView<1> _View)
{
}
template <int t_Flags>
struct TCSignedOwner
{
	int *m_pData;
	~TCSignedOwner()
	{
		if constexpr ((t_Flags & -1) < 0)
			delete m_pData;
	}
};
template struct TCSignedOwner<-1>;
void fg_SignedOwner(TCSignedOwner<-1> _Owner)
// CHECK-MESSAGES: :[[@LINE-1]]:39: warning: parameter '_Owner' takes 'TCSignedOwner<-1>' by value; {{.*}}
{
}
void fg_ReleaseCast(int *_pData);
struct CCastOwner
{
	void *m_pData;
	CCastOwner(CCastOwner const &) = delete;
	~CCastOwner()
	{
		fg_ReleaseCast(static_cast<int *>(m_pData));
	}
};
void fg_CastOwner(CCastOwner _Owner)
// CHECK-MESSAGES: :[[@LINE-1]]:30: warning: parameter '_Owner' takes 'CCastOwner' by value; {{.*}}
{
}
void fg_CheckFailed();
struct CCheckedView
{
	int const *m_pData;
	void f_Check() const
	{
		if (!m_pData)
			fg_CheckFailed();
	}
	~CCheckedView()
	{
		f_Check();
	}
};
void fg_CheckedView(CCheckedView _View)
{
}
namespace NMib::NConcurrency
{
	struct CActor
	{
	};
}
struct CActorOwner
{
	int *m_pData;
	~CActorOwner()
	{
		delete m_pData;
	}
};
struct CActorWorker : NMib::NConcurrency::CActor
{
	void f_Run(CActorOwner _Owner)
	{
	}
};

// Conditions evaluated as C++ converts their parts, a member released through
// a local, and a member only given to what reads it.
template <unsigned t_Flags>
struct TCComplementOwner
{
	int *m_pData;
	~TCComplementOwner()
	{
		if constexpr (~t_Flags == 0u)
			delete m_pData;
	}
};
template struct TCComplementOwner<~0u>;
void fg_ComplementOwner(TCComplementOwner<~0u> _Owner)
// CHECK-MESSAGES: :[[@LINE-1]]:48: warning: parameter '_Owner' takes 'TCComplementOwner<~0U>' by value; {{.*}}
{
}
template <unsigned long long t_Flags>
struct TCWideFlagsView
{
	int const *m_pData;
	~TCWideFlagsView()
	{
		if constexpr ((t_Flags & -1) < 0)
			delete m_pData;
	}
};
void fg_WideFlagsView(TCWideFlagsView<~0ull> _View)
{
}
void fg_ReleaseLocal(int *_pData);
struct CLocalOwner
{
	int *m_pData;
	CLocalOwner(CLocalOwner const &) = delete;
	~CLocalOwner()
	{
		auto pData = m_pData;
		fg_ReleaseLocal(pData);
	}
};
void fg_LocalOwner(CLocalOwner _Owner)
// CHECK-MESSAGES: :[[@LINE-1]]:32: warning: parameter '_Owner' takes 'CLocalOwner' by value; {{.*}}
{
}
bool fg_IsValidData(int const *_pData);
struct CValidatedView
{
	int const *m_pData;
	~CValidatedView()
	{
		if (!fg_IsValidData(m_pData))
			__builtin_trap();
	}
};
void fg_ValidatedView(CValidatedView _View)
{
}

// A const member function that releases anyway, a member class of a template
// whose specialization discards its release, and a constexpr if whose
// initializer does nothing.
struct CConstReleaser
{
	int const *m_pData;
	CConstReleaser(CConstReleaser const &) = delete;
	void f_Release() const
	{
		delete m_pData;
	}
	~CConstReleaser()
	{
		f_Release();
	}
};
void fg_ConstReleaser(CConstReleaser _Owner)
// CHECK-MESSAGES: :[[@LINE-1]]:38: warning: parameter '_Owner' takes 'CConstReleaser' by value; {{.*}}
{
}
template <bool t_bOwns>
struct TCOuterOwner
{
	struct CView
	{
		int *m_pData;
		~CView()
		{
			if constexpr (t_bOwns)
				delete m_pData;
		}
	};
};
void fg_NestedView(TCOuterOwner<false>::CView _View)
{
}
struct CInitializedView
{
	int *m_pData;
	~CInitializedView()
	{
		if constexpr (int Value = 0; false)
			delete m_pData;
	}
};
void fg_InitializedView(CInitializedView _View)
{
}

// A member of a class template whose specializations return what can't be a
// coroutine, a cleanup through a function pointer of the object, and a lambda
// made but not called.
struct CTemplateMemberOwner
{
	int *m_pData;
	CTemplateMemberOwner(CTemplateMemberOwner const &) = delete;
	CTemplateMemberOwner(CTemplateMemberOwner &&);
	~CTemplateMemberOwner()
	{
		delete m_pData;
	}
};
template <typename t_CReturn>
struct TCTemplateMembers
{
	t_CReturn f_Process(CTemplateMemberOwner _Owner)
// CHECK-MESSAGES: :[[@LINE-1]]:43: warning: parameter '_Owner' takes 'CTemplateMemberOwner' by value; {{.*}}
	{
		return {};
	}
};
int fg_UseTemplateMembers(TCTemplateMembers<int> &_Members, CTemplateMemberOwner &&_Owner)
{
	return _Members.f_Process(static_cast<CTemplateMemberOwner &&>(_Owner));
}
template <typename t_FOnExit>
struct TCScopeExitOwner
{
	t_FOnExit mp_fOnExit;
	TCScopeExitOwner(TCScopeExitOwner const &) = delete;
	TCScopeExitOwner(TCScopeExitOwner &&);
	~TCScopeExitOwner()
	{
		mp_fOnExit();
	}
};
template struct TCScopeExitOwner<void (*)()>;
void fg_TakeScopeExit(TCScopeExitOwner<void (*)()> _Exit)
// CHECK-MESSAGES: :[[@LINE-1]]:52: warning: parameter '_Exit' takes 'TCScopeExitOwner<void (*)()>' by value; {{.*}}
{
}
struct CUncalledLambdaView
{
	int const *m_pData;
	~CUncalledLambdaView()
	{
		auto fUnused = [this] { delete m_pData; };
		(void)fUnused;
	}
};
void fg_UncalledLambdaView(CUncalledLambdaView _View)
{
}

// A condition of a constant of a type that is a parameter.
struct CViewOptions
{
	static constexpr bool mc_bOwns = false;
};
template <typename t_COptions>
struct TCOptionsStorage
{
	int *m_pData;
	~TCOptionsStorage()
	{
		if constexpr (t_COptions::mc_bOwns)
			delete m_pData;
	}
};
void fg_OptionsView(TCOptionsStorage<CViewOptions> _View)
{
}

// A const member operator that checks the object, and a condition of `?:`.
void fg_FailedCheck();
struct COperatorCheckedView
{
	int const *m_pData;
	bool operator ! () const
	{
		return !m_pData;
	}
	~COperatorCheckedView()
	{
		if (!*this)
			fg_FailedCheck();
	}
};
void fg_OperatorCheckedView(COperatorCheckedView _View)
{
}
template <bool t_bOwns>
struct TCConditionalStorage
{
	int *m_pData;
	~TCConditionalStorage()
	{
		if constexpr (t_bOwns ? true : false)
			delete m_pData;
	}
};
void fg_ConditionalView(TCConditionalStorage<false> _View)
{
}

// Arithmetic in a condition, a release given to a local of the destructor, and
// a flag of the object given by value.
template <unsigned t_Mode>
struct TCModeStorage
{
	int *m_pData;
	~TCModeStorage()
	{
		if constexpr (t_Mode + 1 > 1)
			delete m_pData;
	}
};
void fg_ModeView(TCModeStorage<0> _View)
{
}
struct CReleaseGuard
{
	int *m_pData;
	~CReleaseGuard()
	{
		delete m_pData;
	}
};
struct CGuardedOwner
{
	int *m_pData;
	CGuardedOwner(CGuardedOwner const &) = delete;
	~CGuardedOwner()
	{
		CReleaseGuard Guard{m_pData};
	}
};
void fg_GuardedOwner(CGuardedOwner _Owner)
// CHECK-MESSAGES: :[[@LINE-1]]:36: warning: parameter '_Owner' takes 'CGuardedOwner' by value; {{.*}}
{
}
void fg_CheckValid(bool _bValid);
struct CFlagCheckedView
{
	int const *m_pData;
	bool m_bValid;
	~CFlagCheckedView()
	{
		fg_CheckValid(m_bValid);
	}
};
void fg_FlagCheckedView(CFlagCheckedView _View)
{
}

// A destructor that makes a local of its own class, a dependent member call
// in a template, a lambda called right away, a handle released by a function,
// and a check that doesn't release.
struct CRound51Reentrant
{
	int m_Depth;
	~CRound51Reentrant()
	{
		if (m_Depth > 0)
		{
			CRound51Reentrant Local{m_Depth - 1};
		}
	}
};
void fg_Round51Reentrant(CRound51Reentrant _Value)
{
}

struct CRound51Storage
{
	int *m_pData;
	void f_Free()
	{
		delete m_pData;
	}
};
template <typename t_CType>
struct TCRound51Owner
{
	t_CType m_Storage;
	TCRound51Owner() = default;
	TCRound51Owner(TCRound51Owner const &) = delete;
	~TCRound51Owner()
	{
		m_Storage.f_Free();
	}
};
template struct TCRound51Owner<CRound51Storage>;
void fg_Round51Template(TCRound51Owner<CRound51Storage> _Value)
// CHECK-MESSAGES: :[[@LINE-1]]:57: warning: parameter '_Value' takes 'TCRound51Owner<CRound51Storage>' by value; take it by 'const &' when the function borrows it or by '&&' when it consumes it [malterlib-parameter-ownership]
{
}

struct CRound51Lambda
{
	int *m_pData;
	CRound51Lambda() = default;
	CRound51Lambda(CRound51Lambda const &) = delete;
	~CRound51Lambda()
	{
		[this] { delete m_pData; }();
	}
};
void fg_Round51Lambda(CRound51Lambda _Value)
// CHECK-MESSAGES: :[[@LINE-1]]:38: warning: parameter '_Value' takes 'CRound51Lambda' by value; take it by 'const &' when the function borrows it or by '&&' when it consumes it [malterlib-parameter-ownership]
{
}

int *g_pRound51Resources[8];
void fg_Round51Release(int _Handle)
{
	delete g_pRound51Resources[_Handle];
}
struct CRound51Handle
{
	int m_Handle;
	CRound51Handle() = default;
	CRound51Handle(CRound51Handle const &) = delete;
	~CRound51Handle()
	{
		fg_Round51Release(m_Handle);
	}
};
void fg_Round51Handle(CRound51Handle _Value)
// CHECK-MESSAGES: :[[@LINE-1]]:38: warning: parameter '_Value' takes 'CRound51Handle' by value; take it by 'const &' when the function borrows it or by '&&' when it consumes it [malterlib-parameter-ownership]
{
}

struct CRound51View
{
	int const *m_pData;
	void f_Check()
	{
		if (!m_pData)
			__builtin_trap();
	}
	~CRound51View()
	{
		f_Check();
	}
};
void fg_Round51View(CRound51View _Value)
{
}

// An element of an array member, a lambda held in a local, an integer given to
// a function that can't be looked into, which can release what it identifies,
// and a branch that the left side of `&&` discards.
void fg_Round52Release(int *_pData);
struct CRound52Array
{
	int *m_pData[1];
	CRound52Array() = default;
	CRound52Array(CRound52Array const &) = delete;
	~CRound52Array()
	{
		fg_Round52Release(m_pData[0]);
	}
};
void fg_Round52Array(CRound52Array _Value)
// CHECK-MESSAGES: :[[@LINE-1]]:36: warning: parameter '_Value' takes 'CRound52Array' by value; take it by 'const &' when the function borrows it or by '&&' when it consumes it [malterlib-parameter-ownership]
{
}

struct CRound52Local
{
	int *m_pData;
	CRound52Local() = default;
	CRound52Local(CRound52Local const &) = delete;
	~CRound52Local()
	{
		auto fRelease = [this] { delete m_pData; };
		fRelease();
	}
};
void fg_Round52Local(CRound52Local _Value)
// CHECK-MESSAGES: :[[@LINE-1]]:36: warning: parameter '_Value' takes 'CRound52Local' by value; take it by 'const &' when the function borrows it or by '&&' when it consumes it [malterlib-parameter-ownership]
{
}

extern "C" int close(int _Descriptor);
struct CRound52File
{
	int m_Descriptor;
	CRound52File(CRound52File const &) = delete;
	~CRound52File()
	{
		close(m_Descriptor);
	}
};
void fg_Round52File(CRound52File _Value)
// CHECK-MESSAGES: :[[@LINE-1]]:34: warning: parameter '_Value' takes 'CRound52File' by value; take it by 'const &' when the function borrows it or by '&&' when it consumes it [malterlib-parameter-ownership]
{
}

template <typename t_CType>
constexpr bool gc_bRound52Supported = sizeof(t_CType) > 1;
template <typename t_CType, bool t_bOwns>
struct TCRound52Storage
{
	int *m_pData;
	~TCRound52Storage()
	{
		if constexpr (t_bOwns && gc_bRound52Supported<t_CType>)
			delete m_pData;
	}
};
void fg_Round52View(TCRound52Storage<int, false> _Value)
{
}

// A wrapper that gives an integer handle on to what can release it releases
// it, but deleting scratch memory of the destructor doesn't own anything.
void fg_Round53Close(int _Handle)
{
	close(_Handle);
}
struct CRound53Handle
{
	int m_Handle;
	CRound53Handle(CRound53Handle const &) = delete;
	~CRound53Handle()
	{
		fg_Round53Close(m_Handle);
	}
};
void fg_Round53Handle(CRound53Handle _Value)
// CHECK-MESSAGES: :[[@LINE-1]]:38: warning: parameter '_Value' takes 'CRound53Handle' by value; take it by 'const &' when the function borrows it or by '&&' when it consumes it [malterlib-parameter-ownership]
{
}

struct CRound53Scratch
{
	int const *m_pData;
	~CRound53Scratch()
	{
		int *pScratch = new int;
		delete pScratch;
	}
};
void fg_Round53Scratch(CRound53Scratch _Value)
{
}

// A helper taking it by `const &` that calls a member function releasing it, a
// count shared through a pointer, and a member template whose specialization
// discards the cleanup.
struct CRound54Resource
{
	int *m_pData;
	void f_Release() const
	{
		delete m_pData;
	}
};
void fg_Round54Release(CRound54Resource const &_Resource)
{
	_Resource.f_Release();
}
struct CRound54IndirectOwner
{
	CRound54Resource m_Resource;
	~CRound54IndirectOwner()
	{
		fg_Round54Release(m_Resource);
	}
};
void fg_Round54Indirect(CRound54IndirectOwner _Value)
// CHECK-MESSAGES: :[[@LINE-1]]:47: warning: parameter '_Value' takes 'CRound54IndirectOwner' by value; take it by 'const &' when the function borrows it or by '&&' when it consumes it [malterlib-parameter-ownership]
{
}

struct CRound54Counter
{
	int *m_pCount;
	~CRound54Counter()
	{
		--*m_pCount;
	}
};
void fg_Round54Counter(CRound54Counter _Value)
// CHECK-MESSAGES: :[[@LINE-1]]:40: warning: parameter '_Value' takes 'CRound54Counter' by value; take it by 'const &' when the function borrows it or by '&&' when it consumes it [malterlib-parameter-ownership]
{
}

template <bool t_bOuter>
struct TCRound54Outer
{
	template <bool t_bOwns>
	struct TCInner
	{
		int *m_pData;
		~TCInner()
		{
			if constexpr (t_bOwns)
				delete m_pData;
		}
	};
};
void fg_Round54View(TCRound54Outer<true>::TCInner<false> _View)
{
}
template struct TCRound54Outer<true>::TCInner<true>;
void fg_Round54Owner(TCRound54Outer<true>::TCInner<true> _Owner)
// CHECK-MESSAGES: :[[@LINE-1]]:58: warning: parameter '_Owner' takes 'TCRound54Outer<true>::TCInner<true>' by value; take it by 'const &' when the function borrows it or by '&&' when it consumes it [malterlib-parameter-ownership]
{
}

// A member of a class in a class template whose instantiation can't be a
// coroutine.
struct CRound55Owner
{
	int *mp_pData;
	~CRound55Owner()
	{
		delete mp_pData;
	}
};
template <typename t_CType>
struct TCRound55Outer
{
	struct CInner
	{
		t_CType f_Run(CRound55Owner _Owner)
// CHECK-MESSAGES: :[[@LINE-1]]:31: warning: parameter '_Owner' takes 'CRound55Owner' by value; take it by 'const &' when the function borrows it or by '&&' when it consumes it [malterlib-parameter-ownership]
		{
			return {};
		}
	};
	template <typename t_CResult>
	t_CResult f_Template(CRound55Owner _Owner)
// CHECK-MESSAGES: :[[@LINE-1]]:37: warning: parameter '_Owner' takes 'CRound55Owner' by value; take it by 'const &' when the function borrows it or by '&&' when it consumes it [malterlib-parameter-ownership]
	{
		return {};
	}
};
void fg_Round55Instantiate()
{
	TCRound55Outer<int>::CInner Inner;
	Inner.f_Run({});
	TCRound55Outer<int> Outer;
	Outer.f_Template<int>({});
}

// A guard that calls a lambda that releases what it captures.
template <typename t_FCleanup>
struct TCRound55Scope
{
	t_FCleanup mp_fCleanup;
	~TCRound55Scope()
	{
		mp_fCleanup();
	}
};
void fg_Round55Guard(int *_pData)
{
	auto fCleanup = [_pData] { delete _pData; };
	using CGuard = TCRound55Scope<decltype(fCleanup)>;
	auto fSink = [](CGuard _Guard) {};
// CHECK-MESSAGES: :[[@LINE-1]]:25: warning: parameter '_Guard' takes 'CGuard' (aka 'TCRound55Scope<{{.*}}>') by value; {{.*}}
	CGuard Guard{fCleanup};
	fSink(Guard);
}

// A destructor that releases through a pointer moved from a member.
struct CRound55Interior
{
	char *mp_pData = new char[2] + 1;
	~CRound55Interior()
	{
		delete[] (mp_pData - 1);
	}
};
void fg_Round55Interior(CRound55Interior _Owner)
// CHECK-MESSAGES: :[[@LINE-1]]:42: warning: parameter '_Owner' takes 'CRound55Interior' by value; take it by 'const &' when the function borrows it or by '&&' when it consumes it [malterlib-parameter-ownership]
{
}

// The arguments of a member template of a specialization for `sizeof` of its
// parameter, a checking operator, a getter of what it releases and a function
// object it calls.
template <typename t_COuter>
struct TCRound56Outer
{
	template <typename t_CInner>
	struct TCInner
	{
		int *mp_pData;
		~TCInner()
		{
			if constexpr (sizeof(t_CInner) == 1)
				delete mp_pData;
		}
	};
};
void fg_Round56Inner(TCRound56Outer<char>::TCInner<int> _Owner)
{
}
struct CRound56View
{
	int *mp_pData;
	bool operator!()
	{
		return mp_pData == nullptr;
	}
	~CRound56View()
	{
		if (!*this)
		{
		}
	}
};
void fg_Round56View(CRound56View _View)
{
}
struct CRound56Getter
{
	int *mp_pData;
	CRound56Getter(CRound56Getter const &) = delete;
	int *f_Get() const
	{
		return mp_pData;
	}
	~CRound56Getter()
	{
		delete f_Get();
	}
};
void fg_Round56Getter(CRound56Getter _Owner)
// CHECK-MESSAGES: :[[@LINE-1]]:38: warning: parameter '_Owner' takes 'CRound56Getter' by value; {{.*}}
{
}
struct CRound56Delete
{
	int *mp_pData;
	void operator()() const
	{
		delete mp_pData;
	}
};
struct CRound56Deleter
{
	int *mp_pData;
	CRound56Deleter(CRound56Deleter const &) = delete;
	~CRound56Deleter()
	{
		CRound56Delete{mp_pData}();
	}
};
void fg_Round56Deleter(CRound56Deleter _Owner)
// CHECK-MESSAGES: :[[@LINE-1]]:40: warning: parameter '_Owner' takes 'CRound56Deleter' by value; {{.*}}
{
}

// A lambda that captures an initialized local, a destructor of a member
// template that the arguments of the template around it tell, a helper that
// only checks a pointer, and a local assigned a member.
template <typename t_FCleanup>
struct TCRound57Guard
{
	t_FCleanup mp_fCleanup;
	TCRound57Guard(t_FCleanup _fCleanup) : mp_fCleanup(_fCleanup) {}
	TCRound57Guard(TCRound57Guard const &) = delete;
	~TCRound57Guard()
	{
		mp_fCleanup();
	}
};
void fg_Round57Capture()
{
	int *pData = new int;
	auto fDelete = [pData] { delete pData; };
	auto fTake = [](TCRound57Guard<decltype(fDelete)> _Guard) {};
// CHECK-MESSAGES: :[[@LINE-1]]:52: warning: parameter '_Guard' takes {{.*}}
	fTake(TCRound57Guard<decltype(fDelete)>{fDelete});
}
template <bool t_bOwns>
struct TCRound57Outer
{
	template <typename t_CTag>
	struct TCInner
	{
		int *mp_pData;
		~TCInner()
		{
			if constexpr (t_bOwns)
				delete mp_pData;
		}
	};
};
void fg_Round57Nested(TCRound57Outer<false>::TCInner<int> _View)
{
}
void fg_Round57CheckPointer(int *_pValue)
{
	if (!_pValue)
		throw 1;
}
struct CRound57View
{
	int *mp_pData;
	~CRound57View()
	{
		fg_Round57CheckPointer(mp_pData);
	}
};
void fg_Round57View(CRound57View _View)
{
}
struct CRound57Owner
{
	int *mp_pData;
	CRound57Owner(CRound57Owner const &) = delete;
	~CRound57Owner()
	{
		int *pData = nullptr;
		pData = mp_pData;
		delete pData;
	}
};
void fg_Round57Owner(CRound57Owner _Owner)
// CHECK-MESSAGES: :[[@LINE-1]]:36: warning: parameter '_Owner' takes 'CRound57Owner' by value; {{.*}}
{
}

// A handle given through a getter, a shared count through a subscript, and a
// result that coroutine_traits makes a coroutine result.
int *g_pRound58Resources[8];
void fg_Round58Close(int _Handle)
{
	delete g_pRound58Resources[_Handle];
}
struct CRound58Handle
{
	int m_Handle;
	CRound58Handle(CRound58Handle const &) = delete;
	int f_Handle() const
	{
		return m_Handle;
	}
	~CRound58Handle()
	{
		fg_Round58Close(f_Handle());
	}
};
void fg_Round58Process(CRound58Handle _Value)
// CHECK-MESSAGES: :[[@LINE-1]]:39: warning: parameter '_Value' takes 'CRound58Handle' by value; {{.*}}
{
}
struct CRound58Count
{
	int *m_pCount;
	CRound58Count(CRound58Count const &_Other) : m_pCount(_Other.m_pCount)
	{
		++m_pCount[0];
	}
	~CRound58Count()
	{
		--m_pCount[0];
	}
};
void fg_Round58Count(CRound58Count _Value)
// CHECK-MESSAGES: :[[@LINE-1]]:36: warning: parameter '_Value' takes 'CRound58Count' by value; {{.*}}
{
}
struct CRound58Owner
{
	int *m_pData;
	~CRound58Owner()
	{
		delete m_pData;
	}
};
namespace std
{
	template <>
	struct coroutine_traits<long, CRound58Owner>
	{
		struct promise_type;
	};
}
long fg_Round58Eligible(CRound58Owner _Value)
{
	return 0;
}

// A comma before what is deleted, option constants inherited from a base,
// a member type of a parameter in `sizeof`, and a dependent call of a const
// member function of the argument.
struct CRound59Comma
{
	int *m_pData;
	bool m_bActive;
	CRound59Comma(CRound59Comma const &) = delete;
	~CRound59Comma()
	{
		delete (m_bActive = false, m_pData);
	}
};
void fg_Round59Comma(CRound59Comma _Value)
// CHECK-MESSAGES: :[[@LINE-1]]:36: warning: parameter '_Value' takes 'CRound59Comma' by value; {{.*}}
{
}
struct CRound59Defaults
{
	static constexpr bool mc_bOwns = false;
};
struct CRound59Options : CRound59Defaults
{
};
struct CRound59Traits
{
	using CChar = int;
};
struct CRound59Data
{
	void f_Check() const
	{
	}
};
template <typename t_CType>
struct TCRound59Inherited
{
	int *m_pData;
	~TCRound59Inherited()
	{
		if constexpr (t_CType::mc_bOwns)
			delete m_pData;
	}
};
template <typename t_CType>
struct TCRound59Sized
{
	int *m_pData;
	~TCRound59Sized()
	{
		if constexpr (sizeof(typename t_CType::CChar) == 1)
			delete m_pData;
	}
};
template <typename t_CType>
struct TCRound59Inspect
{
	t_CType *m_pData;
	~TCRound59Inspect()
	{
		m_pData->f_Check();
	}
};
void fg_Round59Views(TCRound59Inherited<CRound59Options> _Inherited, TCRound59Sized<CRound59Traits> _Sized, TCRound59Inspect<CRound59Data> _Inspect)
{
}

// A destructor that deletes through a copy of a lambda owns, and one that only
// gives the pointer to a helper that checks it, through a const pointer,
// doesn't.
struct CRound60Owner
{
	int *m_pData;
	~CRound60Owner()
	{
		auto fDelete = [this] { delete m_pData; };
		auto fCopy = fDelete;
		fCopy();
	}
};
void fg_Round60Owner(CRound60Owner _Owner)
{
}
// CHECK-MESSAGES: :[[@LINE-3]]:36: warning: parameter '_Owner' takes 'CRound60Owner' by value; {{.*}}
void fg_Round60Check(int const *_pValue)
{
	if (!_pValue)
		throw 1;
}
void fg_Round60CheckWrapper(int *_pValue)
{
	fg_Round60Check(_pValue);
}
struct CRound60View
{
	int *m_pData;
	~CRound60View()
	{
		fg_Round60CheckWrapper(m_pData);
	}
};
void fg_Round60View(CRound60View _View)
{
}

// A helper that releases through a lambda it calls releases, and deleting a
// local pointed at a copy doesn't release the member it was made from.
void fg_Round61Release(int const *_pData)
{
	[_pData] { delete _pData; }();
}
struct CRound61Owner
{
	int *m_pData;
	~CRound61Owner()
	{
		fg_Round61Release(m_pData);
	}
};
void fg_Round61Owner(CRound61Owner _Owner)
{
}
// CHECK-MESSAGES: :[[@LINE-3]]:36: warning: parameter '_Owner' takes 'CRound61Owner' by value; {{.*}}
struct CRound61View
{
	int const *m_pData;
	~CRound61View()
	{
		int const *pCopy = m_pData;
		pCopy = new int(*pCopy);
		delete pCopy;
	}
};
void fg_Round61View(CRound61View _View)
{
}

// A helper whose release is in a discarded branch, or in a lambda it doesn't
// call, doesn't release; a count shared through a reference member is owned;
// and a function that a generic specialization of the coroutine traits is
// for can be a coroutine.
void fg_Round62Discarded(int const *_pData)
{
	if constexpr (false)
		delete _pData;
}
void fg_Round62Uncalled(int const *_pData)
{
	auto fRelease = [_pData] { delete _pData; };
}
struct CRound62Views
{
	int const *m_pData;
	~CRound62Views()
	{
		fg_Round62Discarded(m_pData);
		fg_Round62Uncalled(m_pData);
	}
};
void fg_Round62Views(CRound62Views _Views)
{
}
struct CRound62Token
{
	unsigned &m_nUsers;
	explicit CRound62Token(unsigned &o_nUsers) : m_nUsers(o_nUsers)
	{
		++m_nUsers;
	}
	CRound62Token(CRound62Token const &_Other) : m_nUsers(_Other.m_nUsers)
	{
		++m_nUsers;
	}
	~CRound62Token()
	{
		--m_nUsers;
	}
};
void fg_Round62Token(CRound62Token _Token)
{
}
// CHECK-MESSAGES: :[[@LINE-3]]:36: warning: parameter '_Token' takes 'CRound62Token' by value; {{.*}}
struct CRound62Owner
{
	int *m_pData;
	~CRound62Owner()
	{
		delete m_pData;
	}
};
namespace std
{
	template <typename t_CResult, typename... tp_CParams>
	struct coroutine_traits;
	template <typename t_CResult>
	struct coroutine_traits<t_CResult, CRound62Owner>
	{
		struct promise_type;
	};
}
// A partial specialization of the traits for the function that the
// translation unit doesn't instantiate doesn't make it a coroutine.
int fg_Round62Traits(CRound62Owner _Owner)
// CHECK-MESSAGES: :[[@LINE-1]]:36: warning: parameter '_Owner' takes 'CRound62Owner' by value; {{.*}}
{
	return 0;
}

// A specialization of the coroutine traits for a result is for the parameters
// it names too; a helper that releases through a local assigned later, or
// what a helper returns of what it is given, releases; a scratch allocation of
// a helper, and what isn't evaluated, doesn't.
struct CRound63Owner
{
	int *m_pData;
	~CRound63Owner()
	{
		delete m_pData;
	}
};
namespace std
{
	template <>
	struct coroutine_traits<int, CRound63Owner>
	{
		struct promise_type;
	};
}
int fg_Round63OtherSignature(CRound63Owner _Owner, int _Other)
{
	return _Other;
}
// CHECK-MESSAGES: :[[@LINE-4]]:44: warning: parameter '_Owner' takes 'CRound63Owner' by value; {{.*}}
void fg_Round63Release(int const *_pData)
{
	int const *pData = nullptr;
	pData = _pData;
	delete pData;
}
int const *fg_Round63Get(int const *_pData)
{
	return _pData;
}
void fg_Round63Scratch(int const *_pData)
{
	auto pScratch = new int(*_pData);
	delete pScratch;
}
void fg_Round63Delete(int *_pData);
struct CRound63Released
{
	int const *m_pData;
	~CRound63Released()
	{
		fg_Round63Release(m_pData);
	}
};
struct CRound63Returned
{
	int const *m_pData;
	~CRound63Returned()
	{
		delete fg_Round63Get(m_pData);
	}
};
struct CRound63Views
{
	int *m_pData;
	~CRound63Views()
	{
		fg_Round63Scratch(m_pData);
		(void)noexcept(fg_Round63Delete(m_pData));
	}
};
void fg_Round63Ownership(CRound63Released _Released, CRound63Returned _Returned, CRound63Views _Views)
{
}
// CHECK-MESSAGES: :[[@LINE-3]]:43: warning: parameter '_Released' takes 'CRound63Released' by value; {{.*}}
// CHECK-MESSAGES: :[[@LINE-4]]:71: warning: parameter '_Returned' takes 'CRound63Returned' by value; {{.*}}

// A pointer is what it holds where it is deleted, not what it is assigned
// later, and a helper that returns what it is given, also through a
// conditional, gives it back.
struct CRound64View
{
	int *m_pData;
	~CRound64View()
	{
		int *pData = new int(*m_pData);
		delete pData;
		pData = m_pData;
		if (!pData)
			__builtin_trap();
	}
};
void fg_Round64View(CRound64View _View)
{
}
int *fg_Round64Get(int *_pData)
{
	return _pData ? _pData : nullptr;
}
struct CRound64Owner
{
	int *m_pData;
	~CRound64Owner()
	{
		delete fg_Round64Get(m_pData);
	}
};
void fg_Round64Owner(CRound64Owner _Owner)
{
}
// CHECK-MESSAGES: :[[@LINE-3]]:36: warning: parameter '_Owner' takes 'CRound64Owner' by value; {{.*}}

// A specialization of the coroutine traits that repeats a parameter is for
// equal types only; a helper that deletes only in what isn't evaluated, or
// writes only in a discarded branch, or changes only a local copy of the
// pointer, doesn't release.
struct CRound65Owner
{
	int *m_pData;
	~CRound65Owner()
	{
		delete m_pData;
	}
};
namespace std
{
	template <typename t_CResult, typename t_CParam>
	struct coroutine_traits<t_CResult, t_CParam, t_CParam>
	{
		struct promise_type;
	};
}
void fg_Round65Repeated(CRound65Owner _Owner, int _Count)
{
}
// CHECK-MESSAGES: :[[@LINE-3]]:39: warning: parameter '_Owner' takes 'CRound65Owner' by value; {{.*}}
void fg_Round65Unevaluated(int const *_pData)
{
	(void)noexcept(delete _pData);
}
void fg_Round65Discarded(int *_pData)
{
	if constexpr (false)
		*_pData = 0;
}
void fg_Round65LocalCopy(int *_pData)
{
	int *pLocal = _pData;
	pLocal = nullptr;
	++pLocal;
}
struct CRound65Views
{
	int *m_pData;
	~CRound65Views()
	{
		fg_Round65Unevaluated(m_pData);
		fg_Round65Discarded(m_pData);
		fg_Round65LocalCopy(m_pData);
	}
};
void fg_Round65Views(CRound65Views _Views)
{
}

// A helper that returns either of two parameters gives either; a member
// function that returns a scratch allocation doesn't give what the object
// owns; a specialization of the coroutine traits that repeats the result is
// for that result only; and a member function qualified with `&&` has its
// object by an rvalue reference.
int *fg_Round66Choose(int *_pFirst, int *_pSecond)
{
	return _pFirst ? _pFirst : _pSecond;
}
struct CRound66Owner
{
	int *m_pData;
	~CRound66Owner()
	{
		delete fg_Round66Choose(nullptr, m_pData);
	}
};
void fg_Round66Choice(CRound66Owner _Owner)
{
}
// CHECK-MESSAGES: :[[@LINE-3]]:37: warning: parameter '_Owner' takes 'CRound66Owner' by value; {{.*}}
struct CRound66View
{
	int *m_pValue;
	int *f_New() const
	{
		return new int(*m_pValue);
	}
	~CRound66View()
	{
		delete f_New();
	}
};
void fg_Round66View(CRound66View _View)
{
}
struct CRound66Deleter
{
	int *m_pData;
	~CRound66Deleter()
	{
		delete m_pData;
	}
};
struct CRound66Runner;
namespace std
{
	template <typename t_CType>
	struct coroutine_traits<t_CType, t_CType>
	{
		struct promise_type;
	};
	template <>
	struct coroutine_traits<int, CRound66Runner &&, CRound66Deleter>
	{
		struct promise_type;
	};
}
void fg_Round66Result(CRound66Deleter _Deleter)
{
}
// CHECK-MESSAGES: :[[@LINE-3]]:39: warning: parameter '_Deleter' takes 'CRound66Deleter' by value; {{.*}}
struct CRound66Runner
{
	int f_Run(CRound66Deleter _Deleter) &&
	{
		return 0;
	}
};
// A lambda called right away with what the object owns, which it deletes.
struct CRound67LambdaOwner
{
	int *m_pData;
	~CRound67LambdaOwner()
	{
		[](int *_pData)
			{
				delete _pData;
			}
			(m_pData)
		;
	}
};
void fg_Round67Lambda(CRound67LambdaOwner _Owner)
{
}
// CHECK-MESSAGES: :[[@LINE-3]]:43: warning: parameter '_Owner' takes 'CRound67LambdaOwner' by value; {{.*}}
// A specialization of the coroutine traits whose pack is of a pattern the
// parameters don't match isn't for the function.
struct CRound67Owner
{
	int *m_pData;
	~CRound67Owner()
	{
		delete m_pData;
	}
};
namespace std
{
	template <typename t_CResult, typename ...tp_CParams>
	struct coroutine_traits<t_CResult, tp_CParams *...>
	{
		struct promise_type;
	};
}
void fg_Round67Constrained(CRound67Owner _Owner)
{
}
// CHECK-MESSAGES: :[[@LINE-3]]:42: warning: parameter '_Owner' takes 'CRound67Owner' by value; {{.*}}
void fg_Round67Pattern(int *_pFirst, int *_pSecond, CRound67Owner _Owner)
{
}
// CHECK-MESSAGES: :[[@LINE-3]]:67: warning: parameter '_Owner' takes 'CRound67Owner' by value; {{.*}}
// A guard made from a local copy of what the object owns releases it, also as
// a temporary, but not one made of a scratch copy.
struct CRound68Guard
{
	int *m_pData;
	~CRound68Guard()
	{
		delete m_pData;
	}
};
struct CRound68Owner
{
	int *m_pData;
	CRound68Owner(CRound68Owner const &_Other);
	~CRound68Owner()
	{
		int *pData = m_pData;
		CRound68Guard Guard{pData};
	}
};
void fg_Round68Owner(CRound68Owner _Value)
{
}
// CHECK-MESSAGES: :[[@LINE-3]]:36: warning: parameter '_Value' takes 'CRound68Owner' by value; {{.*}}
struct CRound68TemporaryOwner
{
	int *m_pData;
	CRound68TemporaryOwner(CRound68TemporaryOwner const &_Other);
	~CRound68TemporaryOwner()
	{
		CRound68Guard{m_pData};
	}
};
void fg_Round68Temporary(CRound68TemporaryOwner _Value)
{
}
// CHECK-MESSAGES: :[[@LINE-3]]:49: warning: parameter '_Value' takes 'CRound68TemporaryOwner' by value; {{.*}}
struct CRound68View
{
	int const *m_pData;
	~CRound68View()
	{
		CRound68Guard Scratch{new int(*m_pData)};
	}
};
void fg_Round68View(CRound68View _Value)
{
}
// A helper that releases what it is given through a guard, and a function
// object that deletes only a scratch copy.
struct CRound69Guard
{
	int const *m_pData;
	~CRound69Guard()
	{
		delete m_pData;
	}
};
void fg_Round69Release(int const *_pData)
{
	CRound69Guard Guard{_pData};
}
struct CRound69Owner
{
	int const *m_pData;
	CRound69Owner(CRound69Owner const &_Other) = delete;
	~CRound69Owner()
	{
		fg_Round69Release(m_pData);
	}
};
void fg_Round69Consume(CRound69Owner _Owner)
{
}
// CHECK-MESSAGES: :[[@LINE-3]]:38: warning: parameter '_Owner' takes 'CRound69Owner' by value; {{.*}}
struct CRound69Delete
{
	int *m_pData;
	void operator()() const
	{
		delete m_pData;
	}
};
struct CRound69View
{
	int *m_pData;
	~CRound69View()
	{
		CRound69Delete{new int(*m_pData)}();
	}
};
void fg_Round69View(CRound69View _View)
{
}
// What a helper returns through a local made from what it is given.
int *fg_Round70Identity(int *_pData)
{
	int *pResult = _pData;
	return pResult;
}
struct CRound70Owner
{
	int *m_pData;
	CRound70Owner(CRound70Owner const &_Other) = delete;
	CRound70Owner(CRound70Owner &&_Other);
	~CRound70Owner()
	{
		delete fg_Round70Identity(m_pData);
	}
};
void fg_Round70ByValue(CRound70Owner _Owner)
{
}
// CHECK-MESSAGES: :[[@LINE-3]]:38: warning: parameter '_Owner' takes 'CRound70Owner' by value; {{.*}}
// Destroying what the object holds explicitly, of a dependent type.
template <typename t_CType>
struct TCRound71Storage
{
	alignas(t_CType) unsigned char m_Data[sizeof(t_CType)];
	TCRound71Storage();
	TCRound71Storage(TCRound71Storage &&_Other);
	TCRound71Storage(TCRound71Storage const &_Other) = delete;
	~TCRound71Storage()
	{
		reinterpret_cast<t_CType *>(m_Data)->~t_CType();
	}
};
template struct TCRound71Storage<CRound70Owner>;
void fg_Round71ByValue(TCRound71Storage<CRound70Owner> _Value)
{
}
// CHECK-MESSAGES: :[[@LINE-3]]:56: warning: parameter '_Value' takes 'TCRound71Storage<CRound70Owner>' by value; {{.*}}
// What a helper returns through a cast, and what a constructor releases.
int *fg_Round72Cast(void *_pData)
{
	return static_cast<int *>(_pData);
}
struct CRound72CastOwner
{
	void *m_pData;
	CRound72CastOwner(CRound72CastOwner const &_Other);
	~CRound72CastOwner()
	{
		delete fg_Round72Cast(m_pData);
	}
};
void fg_Round72CastOwner(CRound72CastOwner _Owner)
{
}
// CHECK-MESSAGES: :[[@LINE-3]]:44: warning: parameter '_Owner' takes 'CRound72CastOwner' by value; {{.*}}
struct CRound72Delete
{
	CRound72Delete(int *_pData)
	{
		delete _pData;
	}
};
struct CRound72ConstructOwner
{
	int *m_pData;
	CRound72ConstructOwner(CRound72ConstructOwner const &_Other);
	~CRound72ConstructOwner()
	{
		CRound72Delete Delete(m_pData);
	}
};
void fg_Round72ConstructOwner(CRound72ConstructOwner _Owner)
{
}
// CHECK-MESSAGES: :[[@LINE-3]]:54: warning: parameter '_Owner' takes 'CRound72ConstructOwner' by value; {{.*}}

// What a helper returns on any of its returns, also after an early one.
int *fg_Round73Data(int *_pData)
{
	if (!_pData)
		return nullptr;
	return _pData;
}
struct CRound73Owner
{
	int *m_pData;
	~CRound73Owner()
	{
		delete fg_Round73Data(m_pData);
	}
};
void fg_Round73Owner(CRound73Owner _Owner)
{
}
// CHECK-MESSAGES: :[[@LINE-3]]:36: warning: parameter '_Owner' takes 'CRound73Owner' by value; {{.*}}

// Helpers that call each other many times, and classes that share members,
// are each looked into once.
void fg_Round73Helper0(int const *_pData)
{
	(void)_pData;
}
void fg_Round73Helper1(int const *_pData)
{
	fg_Round73Helper0(_pData);
	fg_Round73Helper0(_pData);
	fg_Round73Helper0(_pData);
	fg_Round73Helper0(_pData);
	fg_Round73Helper0(_pData);
	fg_Round73Helper0(_pData);
	fg_Round73Helper0(_pData);
	fg_Round73Helper0(_pData);
	fg_Round73Helper0(_pData);
	fg_Round73Helper0(_pData);
	fg_Round73Helper0(_pData);
	fg_Round73Helper0(_pData);
	fg_Round73Helper0(_pData);
	fg_Round73Helper0(_pData);
	fg_Round73Helper0(_pData);
	fg_Round73Helper0(_pData);
	fg_Round73Helper0(_pData);
	fg_Round73Helper0(_pData);
	fg_Round73Helper0(_pData);
	fg_Round73Helper0(_pData);
	fg_Round73Helper0(_pData);
	fg_Round73Helper0(_pData);
	fg_Round73Helper0(_pData);
	fg_Round73Helper0(_pData);
	fg_Round73Helper0(_pData);
	fg_Round73Helper0(_pData);
	fg_Round73Helper0(_pData);
	fg_Round73Helper0(_pData);
	fg_Round73Helper0(_pData);
	fg_Round73Helper0(_pData);
}
void fg_Round73Helper2(int const *_pData)
{
	fg_Round73Helper1(_pData);
	fg_Round73Helper1(_pData);
	fg_Round73Helper1(_pData);
	fg_Round73Helper1(_pData);
	fg_Round73Helper1(_pData);
	fg_Round73Helper1(_pData);
	fg_Round73Helper1(_pData);
	fg_Round73Helper1(_pData);
	fg_Round73Helper1(_pData);
	fg_Round73Helper1(_pData);
	fg_Round73Helper1(_pData);
	fg_Round73Helper1(_pData);
	fg_Round73Helper1(_pData);
	fg_Round73Helper1(_pData);
	fg_Round73Helper1(_pData);
	fg_Round73Helper1(_pData);
	fg_Round73Helper1(_pData);
	fg_Round73Helper1(_pData);
	fg_Round73Helper1(_pData);
	fg_Round73Helper1(_pData);
	fg_Round73Helper1(_pData);
	fg_Round73Helper1(_pData);
	fg_Round73Helper1(_pData);
	fg_Round73Helper1(_pData);
	fg_Round73Helper1(_pData);
	fg_Round73Helper1(_pData);
	fg_Round73Helper1(_pData);
	fg_Round73Helper1(_pData);
	fg_Round73Helper1(_pData);
	fg_Round73Helper1(_pData);
}
void fg_Round73Helper3(int const *_pData)
{
	fg_Round73Helper2(_pData);
	fg_Round73Helper2(_pData);
	fg_Round73Helper2(_pData);
	fg_Round73Helper2(_pData);
	fg_Round73Helper2(_pData);
	fg_Round73Helper2(_pData);
	fg_Round73Helper2(_pData);
	fg_Round73Helper2(_pData);
	fg_Round73Helper2(_pData);
	fg_Round73Helper2(_pData);
	fg_Round73Helper2(_pData);
	fg_Round73Helper2(_pData);
	fg_Round73Helper2(_pData);
	fg_Round73Helper2(_pData);
	fg_Round73Helper2(_pData);
	fg_Round73Helper2(_pData);
	fg_Round73Helper2(_pData);
	fg_Round73Helper2(_pData);
	fg_Round73Helper2(_pData);
	fg_Round73Helper2(_pData);
	fg_Round73Helper2(_pData);
	fg_Round73Helper2(_pData);
	fg_Round73Helper2(_pData);
	fg_Round73Helper2(_pData);
	fg_Round73Helper2(_pData);
	fg_Round73Helper2(_pData);
	fg_Round73Helper2(_pData);
	fg_Round73Helper2(_pData);
	fg_Round73Helper2(_pData);
	fg_Round73Helper2(_pData);
}
void fg_Round73Helper4(int const *_pData)
{
	fg_Round73Helper3(_pData);
	fg_Round73Helper3(_pData);
	fg_Round73Helper3(_pData);
	fg_Round73Helper3(_pData);
	fg_Round73Helper3(_pData);
	fg_Round73Helper3(_pData);
	fg_Round73Helper3(_pData);
	fg_Round73Helper3(_pData);
	fg_Round73Helper3(_pData);
	fg_Round73Helper3(_pData);
	fg_Round73Helper3(_pData);
	fg_Round73Helper3(_pData);
	fg_Round73Helper3(_pData);
	fg_Round73Helper3(_pData);
	fg_Round73Helper3(_pData);
	fg_Round73Helper3(_pData);
	fg_Round73Helper3(_pData);
	fg_Round73Helper3(_pData);
	fg_Round73Helper3(_pData);
	fg_Round73Helper3(_pData);
	fg_Round73Helper3(_pData);
	fg_Round73Helper3(_pData);
	fg_Round73Helper3(_pData);
	fg_Round73Helper3(_pData);
	fg_Round73Helper3(_pData);
	fg_Round73Helper3(_pData);
	fg_Round73Helper3(_pData);
	fg_Round73Helper3(_pData);
	fg_Round73Helper3(_pData);
	fg_Round73Helper3(_pData);
}
void fg_Round73Helper5(int const *_pData)
{
	fg_Round73Helper4(_pData);
	fg_Round73Helper4(_pData);
	fg_Round73Helper4(_pData);
	fg_Round73Helper4(_pData);
	fg_Round73Helper4(_pData);
	fg_Round73Helper4(_pData);
	fg_Round73Helper4(_pData);
	fg_Round73Helper4(_pData);
	fg_Round73Helper4(_pData);
	fg_Round73Helper4(_pData);
	fg_Round73Helper4(_pData);
	fg_Round73Helper4(_pData);
	fg_Round73Helper4(_pData);
	fg_Round73Helper4(_pData);
	fg_Round73Helper4(_pData);
	fg_Round73Helper4(_pData);
	fg_Round73Helper4(_pData);
	fg_Round73Helper4(_pData);
	fg_Round73Helper4(_pData);
	fg_Round73Helper4(_pData);
	fg_Round73Helper4(_pData);
	fg_Round73Helper4(_pData);
	fg_Round73Helper4(_pData);
	fg_Round73Helper4(_pData);
	fg_Round73Helper4(_pData);
	fg_Round73Helper4(_pData);
	fg_Round73Helper4(_pData);
	fg_Round73Helper4(_pData);
	fg_Round73Helper4(_pData);
	fg_Round73Helper4(_pData);
}
struct CRound73Checked
{
	int const *m_pData;
	CRound73Checked(CRound73Checked const &_Other);
	~CRound73Checked()
	{
		fg_Round73Helper5(m_pData);
	}
};
void fg_Round73Checked(CRound73Checked _Value)
{
}
struct CRound73Member0
{
	int *m_pData;
	~CRound73Member0()
	{
	}
	CRound73Member0(CRound73Member0 const &_Other);
};
struct CRound73Member1
{
	CRound73Member0 m_First;
	CRound73Member0 m_Second;
	CRound73Member0 m_Third;
};
struct CRound73Member2
{
	CRound73Member1 m_First;
	CRound73Member1 m_Second;
	CRound73Member1 m_Third;
};
struct CRound73Member3
{
	CRound73Member2 m_First;
	CRound73Member2 m_Second;
	CRound73Member2 m_Third;
};
struct CRound73Member4
{
	CRound73Member3 m_First;
	CRound73Member3 m_Second;
	CRound73Member3 m_Third;
};
struct CRound73Member5
{
	CRound73Member4 m_First;
	CRound73Member4 m_Second;
	CRound73Member4 m_Third;
};
struct CRound73Member6
{
	CRound73Member5 m_First;
	CRound73Member5 m_Second;
	CRound73Member5 m_Third;
};
struct CRound73Member7
{
	CRound73Member6 m_First;
	CRound73Member6 m_Second;
	CRound73Member6 m_Third;
};
struct CRound73Member8
{
	CRound73Member7 m_First;
	CRound73Member7 m_Second;
	CRound73Member7 m_Third;
};
struct CRound73Member9
{
	CRound73Member8 m_First;
	CRound73Member8 m_Second;
	CRound73Member8 m_Third;
};
struct CRound73Member10
{
	CRound73Member9 m_First;
	CRound73Member9 m_Second;
	CRound73Member9 m_Third;
};
struct CRound73Member11
{
	CRound73Member10 m_First;
	CRound73Member10 m_Second;
	CRound73Member10 m_Third;
};
struct CRound73Member12
{
	CRound73Member11 m_First;
	CRound73Member11 m_Second;
	CRound73Member11 m_Third;
};
struct CRound73Member13
{
	CRound73Member12 m_First;
	CRound73Member12 m_Second;
	CRound73Member12 m_Third;
};
struct CRound73Release
{
	int *m_pData;
	CRound73Release(CRound73Release const &_Other);
	~CRound73Release()
	{
		delete m_pData;
	}
};
struct CRound73Members
{
	CRound73Member13 m_Members;
	CRound73Release m_Release;
};
// Two parameters, so that no specialization of the coroutine traits above is
// for it.
void fg_Round73Members(int _Count, CRound73Members _Value)
{
}
// CHECK-MESSAGES: :[[@LINE-3]]:52: warning: parameter '_Value' takes 'CRound73Members' by value; {{.*}}

// Destroying what the object holds explicitly releases only what destroying
// the type the specialization gives does.
struct CRound74Empty
{
	~CRound74Empty()
	{
	}
};
template <typename t_CType>
struct TCRound74Value
{
	t_CType m_Value;
	TCRound74Value(TCRound74Value const &_Other);
	~TCRound74Value()
	{
		m_Value.~t_CType();
	}
};
void fg_Round74Empty(int _Count, TCRound74Value<CRound74Empty> _Value)
{
}
template struct TCRound74Value<CRound73Release>;
void fg_Round74Owner(int _Count, TCRound74Value<CRound73Release> _Value)
{
}
// CHECK-MESSAGES: :[[@LINE-3]]:66: warning: parameter '_Value' takes 'TCRound74Value<CRound73Release>' by value; {{.*}}

// A handle given by value to what deletes what it holds releases it, and a
// view whose destructor only clears its own pointer through a helper doesn't.
struct CRound75Handle
{
	int *m_pData;
};
void fg_Round75Close(CRound75Handle _Handle)
{
	delete _Handle.m_pData;
}
void fg_Round75Log(CRound75Handle _Handle)
{
}
struct CRound75Owner
{
	CRound75Handle m_Handle;
	CRound75Owner(CRound75Owner const &_Other);
	~CRound75Owner()
	{
		fg_Round75Log(m_Handle);
		fg_Round75Close(m_Handle);
	}
};
struct CRound75Logged
{
	CRound75Handle m_Handle;
	CRound75Logged(CRound75Logged const &_Other);
	~CRound75Logged()
	{
		fg_Round75Log(m_Handle);
	}
};
struct CRound75View;
void fg_Round75Reset(CRound75View *_pView);
struct CRound75View
{
	int *m_pData;
	~CRound75View()
	{
		fg_Round75Reset(this);
	}
};
void fg_Round75Reset(CRound75View *_pView)
{
	_pView->m_pData = nullptr;
}
void fg_Round75Values(int _Count, CRound75Owner _Owner, CRound75Logged _Logged, CRound75View _View)
{
}
// CHECK-MESSAGES: :[[@LINE-3]]:49: warning: parameter '_Owner' takes 'CRound75Owner' by value; {{.*}}

// A lambda that deletes a scratch copy it captures doesn't release what the
// object holds.
struct CRound76View
{
	int const *m_pData;
	~CRound76View()
	{
		[pScratch = new int(*m_pData)]
			{
				delete pScratch;
			}
			()
		;
	}
};
void fg_Round76Borrow(int _Count, CRound76View _View)
{
}

// A copy of a lambda that deletes deletes too.
void fg_Round77Release(int *_pData)
{
	auto fRelease = [_pData] { delete _pData; };
	auto fCopy = fRelease;
	fCopy();
}
struct CRound77Owner
{
	int *m_pData;
	CRound77Owner(CRound77Owner const &_Other) = delete;
	~CRound77Owner()
	{
		fg_Round77Release(m_pData);
	}
};
void fg_Round77Consume(CRound77Owner _Owner) {}
// CHECK-MESSAGES: :[[@LINE-1]]:38: warning: parameter '_Owner' takes 'CRound77Owner' by value; take it by 'const &' when the function borrows it or by '&&' when it consumes it [malterlib-parameter-ownership]

// Clearing its own member through the object given, also dereferenced or
// through a local, releases nothing.
struct CRound77View;
void fg_Round77Reset(CRound77View *_pView);
struct CRound77View
{
	int *m_pData;
	int *m_pOther;
	~CRound77View()
	{
		fg_Round77Reset(this);
	}
};
void fg_Round77Reset(CRound77View *_pView)
{
	(*_pView).m_pData = nullptr;
	CRound77View *pView = _pView;
	pView->m_pOther = nullptr;
}
void fg_Round77Borrow(CRound77View _View) {}

// Variadic arguments that the function doesn't read.
void fg_Round78Log(char const *_pFormat, ...)
{
}
struct CRound78View
{
	int *m_pData;
	~CRound78View()
	{
		fg_Round78Log("View", m_pData);
	}
};
void fg_Round78Walk(CRound78View _View) {}

// A copy of a value read through the pointer given to a helper.
bool fg_Round79IsPositive(int _Value)
{
	return _Value > 0;
}
void fg_Round79Check(int *_pValue)
{
	if (!fg_Round79IsPositive(*_pValue))
		__builtin_trap();
}
struct CRound79View
{
	int *m_pValue;
	~CRound79View()
	{
		fg_Round79Check(m_pValue);
	}
};
void fg_Round79Walk(CRound79View _View) {}

// A scope guard whose destructor releases what the thread has.
void fg_Round80Lock();
void fg_Round80Unlock();
struct CRound80Guard
{
	CRound80Guard()
	{
		fg_Round80Lock();
	}
	~CRound80Guard()
	{
		fg_Round80Unlock();
	}
};
void fg_Round80Run(CRound80Guard _Guard) {}
// CHECK-MESSAGES: :[[@LINE-1]]:34: warning: parameter '_Guard' takes 'CRound80Guard' by value; take it by 'const &' when the function borrows it or by '&&' when it consumes it [malterlib-parameter-ownership]

// A guard that releases in a branch, and a count released through a local
// reference.
void fg_Round81Unlock();
struct CRound81Guard
{
	bool m_bActive;
	explicit CRound81Guard(bool _bActive) : m_bActive(_bActive) {}
	CRound81Guard(CRound81Guard const &_Other) = delete;
	~CRound81Guard()
	{
		if (m_bActive)
			fg_Round81Unlock();
	}
};
void fg_Round81Take(CRound81Guard _Guard) {}
// CHECK-MESSAGES: :[[@LINE-1]]:35: warning: parameter '_Guard' takes 'CRound81Guard' by value; take it by 'const &' when the function borrows it or by '&&' when it consumes it [malterlib-parameter-ownership]
struct CRound81Counted
{
	unsigned *m_pCount;
	CRound81Counted(CRound81Counted const &_Other) : m_pCount(_Other.m_pCount)
	{
		++*m_pCount;
	}
	~CRound81Counted()
	{
		auto &Count = *m_pCount;
		--Count;
	}
};
void fg_Round81Borrow(CRound81Counted _Owner) {}
// CHECK-MESSAGES: :[[@LINE-1]]:39: warning: parameter '_Owner' takes 'CRound81Counted' by value; take it by 'const &' when the function borrows it or by '&&' when it consumes it [malterlib-parameter-ownership]

// A discarded branch of a destructor releases nothing, and a loop of a guard
// that isn't copied as is can.
void fg_Round82Unlock();
struct CRound82View
{
	int *m_pData;
	CRound82View(CRound82View const &_Other) : m_pData(_Other.m_pData) {}
	~CRound82View()
	{
		if constexpr (false)
			fg_Round82Unlock();
	}
};
void fg_Round82Borrow(CRound82View _View) {}
struct CRound82Guard
{
	unsigned m_nLocks;
	explicit CRound82Guard(unsigned _nLocks) : m_nLocks(_nLocks) {}
	CRound82Guard(CRound82Guard const &_Other) = delete;
	~CRound82Guard()
	{
		while (m_nLocks != 0)
		{
			fg_Round82Unlock();
			--m_nLocks;
		}
	}
};
void fg_Round82Take(CRound82Guard _Guard) {}
// CHECK-MESSAGES: :[[@LINE-1]]:35: warning: parameter '_Guard' takes 'CRound82Guard' by value; take it by 'const &' when the function borrows it or by '&&' when it consumes it [malterlib-parameter-ownership]

// Writing through a pointer to a member of the object itself changes only
// the object.
struct CRound83View
{
	int *m_pData;
	~CRound83View()
	{
		auto ppData = &m_pData;
		*ppData = nullptr;
	}
};
void fg_Round83Borrow(CRound83View _View) {}

// A branch of an `if constexpr` that the arguments of a specialization
// discard, a change of what the program has, a pointer to a member of the
// object itself through a subscript, and a copy that the class makes itself.
void fg_Round84Unlock();
template <bool t_bOwns>
struct TCRound84Guard
{
	TCRound84Guard(TCRound84Guard const &_Other) {}
	~TCRound84Guard()
	{
		if constexpr (t_bOwns)
			fg_Round84Unlock();
	}
};
void fg_Round84Borrow(TCRound84Guard<false> _View) {}
template struct TCRound84Guard<true>;
void fg_Round84Take(TCRound84Guard<true> _Guard) {}
// CHECK-MESSAGES: :[[@LINE-1]]:42: warning: parameter '_Guard' takes 'TCRound84Guard<true>' by value; take it by 'const &' when the function borrows it or by '&&' when it consumes it [malterlib-parameter-ownership]
unsigned g_nRound84Users;
struct CRound84Lease
{
	CRound84Lease() { ++g_nRound84Users; }
	CRound84Lease(CRound84Lease const &_Other) { ++g_nRound84Users; }
	~CRound84Lease() { --g_nRound84Users; }
};
void fg_Round84Lease(CRound84Lease _Lease) {}
// CHECK-MESSAGES: :[[@LINE-1]]:36: warning: parameter '_Lease' takes 'CRound84Lease' by value; take it by 'const &' when the function borrows it or by '&&' when it consumes it [malterlib-parameter-ownership]
struct CRound84View
{
	int *m_pData;
	~CRound84View()
	{
		auto ppData = &m_pData;
		ppData[0] = nullptr;
	}
};
void fg_Round84Walk(CRound84View _View) {}
struct CRound84Owner
{
	int *m_pData;
	CRound84Owner(CRound84Owner const &_Other) : m_pData(new int(*_Other.m_pData)) {}
	~CRound84Owner() { delete m_pData; }
};
struct CRound84Borrowed
{
	CRound84Owner const *m_pOwner;
	~CRound84Borrowed() { CRound84Owner Scratch(*m_pOwner); }
};
void fg_Round84Borrowed(CRound84Borrowed _View) {}

// Released through a structured binding, and a branch that an enumerator of
// the arguments of a specialization discards.
struct CRound85Owner
{
	int *m_pData;
	~CRound85Owner()
	{
		auto &[pData] = *this;
		delete pData;
	}
};
void fg_Round85Take(CRound85Owner _Owner) {}
// CHECK-MESSAGES: :[[@LINE-1]]:35: warning: parameter '_Owner' takes 'CRound85Owner' by value; take it by 'const &' when the function borrows it or by '&&' when it consumes it [malterlib-parameter-ownership]
struct CRound85Options
{
	enum
	{
		EOwns = false
	};
};
template <typename t_COptions>
struct TCRound85Storage
{
	int *m_pData;
	~TCRound85Storage()
	{
		if constexpr (t_COptions::EOwns)
			delete m_pData;
	}
};
void fg_Round85View(TCRound85Storage<CRound85Options> _View) {}

// A lambda called through a pointer releases what it captures.
struct CRound86Owner
{
	int *m_pData;
	explicit CRound86Owner(int *_pData) : m_pData(_pData) {}
	CRound86Owner(CRound86Owner const &_Other) = delete;
	~CRound86Owner()
	{
		auto fDelete = [this] { delete m_pData; };
		auto pDelete = &fDelete;
		(*pDelete)();
	}
};
void fg_Round86Take(CRound86Owner _Owner)
// CHECK-MESSAGES: :[[@LINE-1]]:35: warning: parameter '_Owner' takes 'CRound86Owner' by value; take it by 'const &' when the function borrows it or by '&&' when it consumes it [malterlib-parameter-ownership]
{
}

// Changing an array of its own releases nothing, but a release in
// parentheses does.
struct CRound87View
{
	int const *m_pData;
	int m_Cache[2];
	~CRound87View()
	{
		m_Cache[0] = 0;
	}
};
void fg_Round87Read(CRound87View _View)
{
}
void fg_Round87Lock();
void fg_Round87Unlock();
struct CRound87Guard
{
	CRound87Guard()
	{
		fg_Round87Lock();
	}
	CRound87Guard(CRound87Guard const &_Other) = delete;
	~CRound87Guard()
	{
		(fg_Round87Unlock());
	}
};
void fg_Round87Run(CRound87Guard _Guard)
// CHECK-MESSAGES: :[[@LINE-1]]:34: warning: parameter '_Guard' takes 'CRound87Guard' by value; take it by 'const &' when the function borrows it or by '&&' when it consumes it [malterlib-parameter-ownership]
{
}

// A helper given more that releases what the program has releases it.
int *g_pRound88Resource;
void fg_Round88Release(bool _bActive)
{
	if (_bActive)
	{
		delete g_pRound88Resource;
		g_pRound88Resource = nullptr;
	}
}
struct CRound88Guard
{
	bool m_bActive;
	CRound88Guard(CRound88Guard const &_Other) = delete;
	~CRound88Guard()
	{
		fg_Round88Release(m_bActive);
	}
};
void fg_Round88Consume(CRound88Guard _Guard)
// CHECK-MESSAGES: :[[@LINE-1]]:38: warning: parameter '_Guard' takes 'CRound88Guard' by value; take it by 'const &' when the function borrows it or by '&&' when it consumes it [malterlib-parameter-ownership]
{
}

// A helper that only resets a member of the object, given its address,
// releases nothing.
void fg_Round89Reset(int **_ppValue)
{
	*_ppValue = nullptr;
}
struct CRound89View
{
	int *m_pData;
	~CRound89View()
	{
		fg_Round89Reset(&m_pData);
	}
};
void fg_Round89Read(CRound89View _View)
{
}

// A pointer kept with tag bits in an integer, which the destructor deletes.
using CRound90Pointer = __UINTPTR_TYPE__;
struct CRound90Tagged
{
	CRound90Pointer m_Pointer;
	explicit CRound90Tagged(int *_pValue)
		: m_Pointer(reinterpret_cast<CRound90Pointer>(_pValue))
	{
	}
	CRound90Tagged(CRound90Tagged const &_Other) = delete;
	~CRound90Tagged()
	{
		delete reinterpret_cast<int *>(m_Pointer & ~CRound90Pointer(1));
	}
};
void fg_Round90Tagged(CRound90Tagged _Tagged)
// CHECK-MESSAGES: :[[@LINE-1]]:38: warning: parameter '_Tagged' takes 'CRound90Tagged' by value; take it by 'const &' when the function borrows it or by '&&' when it consumes it [malterlib-parameter-ownership]
{
}

// A member of its own reset through a reference, which releases nothing.
void fg_Round90Reset(int *&o_pData)
{
	o_pData = nullptr;
}
struct CRound90View
{
	int *m_pData;
	~CRound90View()
	{
		fg_Round90Reset(m_pData);
	}
};
void fg_Round90View(CRound90View _View)
{
}

// A scratch copy that a lambda the destructor calls deletes.
struct CRound91View
{
	int const *m_pData;
	~CRound91View()
	{
		int *pScratch = new int(*m_pData);
		[pScratch]
			{
				delete pScratch;
			}
		();
	}
};
void fg_Round91View(CRound91View _View)
{
}

// Writing a local copy of a value doesn't change the object.
void fg_Round92Reset(int *o_pValue)
{
	*o_pValue = 0;
}
struct CRound92View
{
	int const *m_pData;
	~CRound92View()
	{
		int Value = *m_pData;
		fg_Round92Reset(&Value);
	}
};
void fg_Round92View(CRound92View _View)
{
}

// A destructor that catches what it calls throws doesn't release anything
// global when it throws, so the guard is still owned.
void fg_Round93Lock();
void fg_Round93Unlock();

struct CRound93Guard
{
	CRound93Guard()
	{
		fg_Round93Lock();
	}
	CRound93Guard(CRound93Guard const &_Other) = delete;

	~CRound93Guard()
	{
		try
		{
			fg_Round93Unlock();
		}
		catch (...)
		{
		}
	}
};

void fg_Round93Consume(CRound93Guard _Guard)
// CHECK-MESSAGES: :[[@LINE-1]]:38: warning: parameter '_Guard' takes 'CRound93Guard' by value; take it by 'const &' when the function borrows it or by '&&' when it consumes it [malterlib-parameter-ownership]
{
}

// Writing an array member through a local pointer changes only the object,
// but releasing what a member points to through a local array owns it.
struct CRound94Cache
{
	int m_Cache[1];
	~CRound94Cache()
	{
		int *pCache = m_Cache;
		pCache[0] = 0;
	}
};

void fg_Round94Cache(CRound94Cache _Value)
{
}

struct CRound94Owner
{
	explicit CRound94Owner(int *_pData)
		: m_pData(_pData)
	{
	}
	CRound94Owner(CRound94Owner const &_Other) = delete;

	~CRound94Owner()
	{
		int *Pointers[] = {m_pData};
		delete Pointers[0];
	}

	int *m_pData;
};

void fg_Round94Owner(CRound94Owner _Owner)
// CHECK-MESSAGES: :[[@LINE-1]]:36: warning: parameter '_Owner' takes 'CRound94Owner' by value; take it by 'const &' when the function borrows it or by '&&' when it consumes it [malterlib-parameter-ownership]
{
}

// A handle kept encoded, `m_FilePlusOne - 1`, is still owned.
extern "C" int close(int _File);

struct CRound95File
{
	explicit CRound95File(int _File)
		: m_FilePlusOne(_File + 1)
	{
	}
	CRound95File(CRound95File const &_Other) = delete;
	~CRound95File()
	{
		close(m_FilePlusOne - 1);
	}

	int m_FilePlusOne;
};

void fg_Round95Consume(CRound95File _File)
// CHECK-MESSAGES: :[[@LINE-1]]:37: warning: parameter '_File' takes 'CRound95File' by value; take it by 'const &' when the function borrows it or by '&&' when it consumes it [malterlib-parameter-ownership]
{
}

// A helper that releases nothing doesn't make a view owning.
bool fg_Round96IsDebugEnabled()
{
	return false;
}

struct CRound96View
{
	~CRound96View()
	{
		(void)fg_Round96IsDebugEnabled();
	}

	int *m_pData;
};

void fg_Round96View(CRound96View _View)
{
}

// A getter that returns its member only in a discarded branch makes a scratch
// copy, and a release in a comma is a release.
struct CRound97View
{
	int *f_Scratch() const
	{
		if constexpr (false)
			return m_pData;
		return new int(*m_pData);
	}

	~CRound97View()
	{
		delete f_Scratch();
	}

	int *m_pData;
};

void fg_Round97Read(CRound97View _View)
{
}

int *g_pRound97Resource;

void fg_Round97Release()
{
	delete g_pRound97Resource;
	g_pRound97Resource = nullptr;
}

struct CRound97Guard
{
	CRound97Guard()
	{
		g_pRound97Resource = new int;
	}
	CRound97Guard(CRound97Guard const &_Other) = delete;
	~CRound97Guard()
	{
		(fg_Round97Release(), void());
	}
};

void fg_Round97Take(CRound97Guard _Guard)
// CHECK-MESSAGES: :[[@LINE-1]]:35: warning: parameter '_Guard' takes 'CRound97Guard' by value; take it by 'const &' when the function borrows it or by '&&' when it consumes it [malterlib-parameter-ownership]
{
}

// A function object of a known type that does nothing, called in a destructor
// of a template that isn't instantiated.
struct CRound98NoOp
{
	void operator () () const
	{
	}
};

template <typename t_FOnExit>
struct TCRound98View
{
	t_FOnExit m_fOnExit;
	~TCRound98View()
	{
		m_fOnExit();
	}
};

void fg_Round98Read(TCRound98View<CRound98NoOp> _View)
{
}

// A guard that unlocks in a case of a `switch`.
void fg_Round99Lock();
void fg_Round99Unlock();

struct CRound99Guard
{
	int m_State;
	CRound99Guard()
		: m_State(1)
	{
		fg_Round99Lock();
	}
	CRound99Guard(CRound99Guard const &_Other) = delete;
	~CRound99Guard()
	{
		switch (m_State)
		{
		case 1:
			fg_Round99Unlock();
			break;
		default:
			break;
		}
	}
};

void fg_Round99Consume(CRound99Guard _Guard)
// CHECK-MESSAGES: :[[@LINE-1]]:38: warning: parameter '_Guard' takes 'CRound99Guard' by value; take it by 'const &' when the function borrows it or by '&&' when it consumes it [malterlib-parameter-ownership]
{
}

// The members of a union are destroyed only explicitly.
struct CRound100Owner
{
	int *m_pData;
	~CRound100Owner()
	{
		delete m_pData;
	}
};

union CRound100Value
{
	int m_Number;
	CRound100Owner m_Unused;
	CRound100Value(int _Number)
		: m_Number(_Number)
	{
	}
	CRound100Value(CRound100Value const &_Other)
		: m_Number(_Other.m_Number)
	{
	}
	~CRound100Value()
	{
	}
};

int fg_Round100Read(CRound100Value _Value)
{
	return _Value.m_Number;
}

// Released through a global pointer to a function.
int g_nRound100Locks;
void fg_Round100Lock()
{
	++g_nRound100Locks;
}
void fg_Round100Unlock()
{
	--g_nRound100Locks;
}
void (*g_fRound100Lock)() = fg_Round100Lock;
void (*g_fRound100Unlock)() = fg_Round100Unlock;

struct CRound100Guard
{
	CRound100Guard()
	{
		g_fRound100Lock();
	}
	CRound100Guard(CRound100Guard const &_Other) = delete;
	~CRound100Guard()
	{
		g_fRound100Unlock();
	}
};

void fg_Round100Work(CRound100Guard _Guard)
// CHECK-MESSAGES: :[[@LINE-1]]:37: warning: parameter '_Guard' takes 'CRound100Guard' by value; take it by 'const &' when the function borrows it or by '&&' when it consumes it [malterlib-parameter-ownership]
{
}

// A guard that deletes a global.
int *g_pRound101Data;

struct CRound101Guard
{
	CRound101Guard()
	{
		g_pRound101Data = new int;
	}
	CRound101Guard(CRound101Guard const &_Other) = delete;
	~CRound101Guard()
	{
		delete g_pRound101Data;
	}
};

void fg_Round101Work(CRound101Guard _Guard)
// CHECK-MESSAGES: :[[@LINE-1]]:37: warning: parameter '_Guard' takes 'CRound101Guard' by value; take it by 'const &' when the function borrows it or by '&&' when it consumes it [malterlib-parameter-ownership]
{
}

// A capture assigned a scratch copy that is deleted.
struct CRound101View
{
	int *m_pData;

	~CRound101View()
	{
		[pData = m_pData]() mutable
		{
			pData = new int(*pData);
			delete pData;
		}();
	}
};

void fg_Round101Read(CRound101View _View)
{
}

// A guard released through an element of a local array.
struct CRound102Delete
{
	int *m_pData;
	~CRound102Delete()
	{
		delete m_pData;
	}
};

struct CRound102Owner
{
	int *m_pData;
	CRound102Owner(CRound102Owner const &_Other) = delete;
	~CRound102Owner()
	{
		CRound102Delete Guards[] = {{m_pData}};
	}
};

void fg_Round102Work(CRound102Owner _Owner)
// CHECK-MESSAGES: :[[@LINE-1]]:37: warning: parameter '_Owner' takes 'CRound102Owner' by value; take it by 'const &' when the function borrows it or by '&&' when it consumes it [malterlib-parameter-ownership]
{
}

// Destroying a scalar releases nothing.
using CRound102Int = int;

struct CRound102Scalar
{
	int m_Value;
	~CRound102Scalar()
	{
		m_Value.~CRound102Int();
	}
};

void fg_Round102Scalar(CRound102Scalar _Value)
{
}

// Released through the address a helper gives back.
int **fg_Round103Pointer(int *&_pData)
{
	return &_pData;
}

struct CRound103Owner
{
	int *m_pData;
	CRound103Owner(CRound103Owner const &_Other) = delete;
	~CRound103Owner()
	{
		delete *fg_Round103Pointer(m_pData);
	}
};

void fg_Round103Run(CRound103Owner _Owner)
// CHECK-MESSAGES: :[[@LINE-1]]:36: warning: parameter '_Owner' takes 'CRound103Owner' by value; take it by 'const &' when the function borrows it or by '&&' when it consumes it [malterlib-parameter-ownership]
{
}

// A constant of the template made from its parameters.
template <bool t_bOwns>
struct TCRound104Storage
{
	static constexpr bool mc_bOwns = t_bOwns;
	int *m_pData;
	~TCRound104Storage()
	{
		if constexpr (mc_bOwns)
			delete m_pData;
	}
};

void fg_Round104View(TCRound104Storage<false> _View)
{
}

// A destructor that gives its pointer to an observer only clears its own
// pointer.
void fg_Round105Observe(int *const *_ppData)
{
}

void fg_Round105Reset(int **_ppData)
{
	*_ppData = nullptr;
	fg_Round105Observe(_ppData);
}

struct CRound105ObservedView
{
	int *m_pData;
	~CRound105ObservedView()
	{
		fg_Round105Reset(&m_pData);
	}
};

void fg_Round105ProcessObserved(CRound105ObservedView _View)
{
}

// A destructor that makes a scratch deep copy only releases the copy.
struct CRound105Owner
{
	int *m_pData;
	CRound105Owner(int const *_pData)
		: m_pData(new int(*_pData))
	{
	}
	~CRound105Owner()
	{
		delete m_pData;
	}
};

struct CRound105ScratchView
{
	int const *m_pData;
	~CRound105ScratchView()
	{
		CRound105Owner Scratch(m_pData);
	}
};

void fg_Round105ProcessScratch(CRound105ScratchView _View)
{
}

// What a member function returns a copy of, which the destructor releases.
struct CRound107State
{
	int *m_pData;
};

struct CRound107Owner
{
	CRound107State m_State;
	CRound107Owner(int *_pData)
		: m_State{_pData}
	{
	}
	CRound107Owner(CRound107Owner const &) = delete;
	CRound107State f_GetState() const
	{
		return m_State;
	}
	~CRound107Owner()
	{
		delete f_GetState().m_pData;
	}
};

void fg_Round107Take(CRound107Owner _Owner)
// CHECK-MESSAGES: :[[@LINE-1]]:37: warning: parameter '_Owner' takes 'CRound107Owner' by value; take it by 'const &' when the function borrows it or by '&&' when it consumes it [malterlib-parameter-ownership]
{
}

// Atomic releases of a count, also through an atomic_ref.
namespace std
{
	template <typename t_CType>
	struct atomic_ref
	{
		explicit atomic_ref(t_CType &_Value);
		t_CType fetch_sub(t_CType _Value) const noexcept;
		t_CType load() const noexcept;
	};
}

struct CRound108AtomicOwner
{
	unsigned *m_pCount;
	CRound108AtomicOwner(CRound108AtomicOwner const &_Other);
	~CRound108AtomicOwner()
	{
		__atomic_fetch_sub(m_pCount, 1u, __ATOMIC_RELAXED);
	}
};

void fg_Round108TakeAtomic(CRound108AtomicOwner _Owner)
// CHECK-MESSAGES: :[[@LINE-1]]:49: warning: parameter '_Owner' takes 'CRound108AtomicOwner' by value; take it by 'const &' when the function borrows it or by '&&' when it consumes it [malterlib-parameter-ownership]
{
}

struct CRound108RefOwner
{
	unsigned *m_pCount;
	CRound108RefOwner(CRound108RefOwner const &_Other);
	~CRound108RefOwner()
	{
		std::atomic_ref<unsigned>(*m_pCount).fetch_sub(1);
	}
};

void fg_Round108TakeRef(CRound108RefOwner _Owner)
// CHECK-MESSAGES: :[[@LINE-1]]:43: warning: parameter '_Owner' takes 'CRound108RefOwner' by value; take it by 'const &' when the function borrows it or by '&&' when it consumes it [malterlib-parameter-ownership]
{
}

// A release through an overloaded operator of the object.
struct CRound108Subscript
{
	int *m_pData;
	int *operator[](unsigned _Index) const
	{
		return m_pData;
	}
	~CRound108Subscript()
	{
		delete (*this)[0];
	}
};

void fg_Round108TakeSubscript(CRound108Subscript _Owner)
// CHECK-MESSAGES: :[[@LINE-1]]:50: warning: parameter '_Owner' takes 'CRound108Subscript' by value; take it by 'const &' when the function borrows it or by '&&' when it consumes it [malterlib-parameter-ownership]
{
}

// A cleanup of a member or an element of a global.
struct CRound108State
{
	int *m_pData;
	unsigned m_nUsers;
};
CRound108State g_Round108State;
int *g_Round108Pointers[2];

struct CRound108MemberGuard
{
	CRound108MemberGuard(CRound108MemberGuard const &_Other) = delete;
	~CRound108MemberGuard()
	{
		delete g_Round108State.m_pData;
	}
};

struct CRound108ElementGuard
{
	CRound108ElementGuard(CRound108ElementGuard const &_Other) = delete;
	~CRound108ElementGuard()
	{
		delete g_Round108Pointers[0];
	}
};

struct CRound108CountGuard
{
	CRound108CountGuard(CRound108CountGuard const &_Other) = delete;
	~CRound108CountGuard()
	{
		--g_Round108State.m_nUsers;
	}
};

void fg_Round108TakeMember(CRound108MemberGuard _Guard)
// CHECK-MESSAGES: :[[@LINE-1]]:49: warning: parameter '_Guard' takes 'CRound108MemberGuard' by value; take it by 'const &' when the function borrows it or by '&&' when it consumes it [malterlib-parameter-ownership]
{
}

void fg_Round108TakeElement(CRound108ElementGuard _Guard)
// CHECK-MESSAGES: :[[@LINE-1]]:51: warning: parameter '_Guard' takes 'CRound108ElementGuard' by value; take it by 'const &' when the function borrows it or by '&&' when it consumes it [malterlib-parameter-ownership]
{
}

void fg_Round108TakeCount(CRound108CountGuard _Guard)
// CHECK-MESSAGES: :[[@LINE-1]]:47: warning: parameter '_Guard' takes 'CRound108CountGuard' by value; take it by 'const &' when the function borrows it or by '&&' when it consumes it [malterlib-parameter-ownership]
{
}

// A local pointer pointed at an external count after it is made.
struct CRound109Reassigned
{
	unsigned m_Local{};
	unsigned *m_pCount;
	CRound109Reassigned(unsigned *_pCount)
		: m_pCount(_pCount)
	{
	}
	CRound109Reassigned(CRound109Reassigned const &_Other) = delete;
	~CRound109Reassigned()
	{
		unsigned *pCount = &m_Local;
		pCount = m_pCount;
		--*pCount;
	}
};

void fg_Round109TakeReassigned(CRound109Reassigned _Owner)
// CHECK-MESSAGES: :[[@LINE-1]]:52: warning: parameter '_Owner' takes 'CRound109Reassigned' by value; take it by 'const &' when the function borrows it or by '&&' when it consumes it [malterlib-parameter-ownership]
{
}

// An element of an array member written through an offset, which is in the
// object.
struct CRound109Cache
{
	int m_Data[2];
	~CRound109Cache()
	{
		*(m_Data + 1) = 0;
	}
};

void fg_Round109ReadCache(CRound109Cache _Cache)
{
}

// A scope guard whose lambda captures the object releases what it is of.
template <typename t_FOnExit>
struct TCRound110OnScopeExit
{
	t_FOnExit m_fOnExit;
	~TCRound110OnScopeExit()
	{
		m_fOnExit();
	}
};

struct CRound110GuardOwner
{
	int *m_pData;
	CRound110GuardOwner(CRound110GuardOwner const &) = delete;
	~CRound110GuardOwner()
	{
		TCRound110OnScopeExit Guard{[this] { delete m_pData; }};
	}
};

void fg_Round110TakeGuarded(CRound110GuardOwner _Owner)
// CHECK-MESSAGES: :[[@LINE-1]]:49: warning: parameter '_Owner' takes 'CRound110GuardOwner' by value; take it by 'const &' when the function borrows it or by '&&' when it consumes it [malterlib-parameter-ownership]
{
}

// A guard that keeps a pointer moved along what it is given.
struct CRound110OffsetGuard
{
	int *m_pData;
	CRound110OffsetGuard(int *_pData)
		: m_pData(_pData + 1)
	{
	}
	CRound110OffsetGuard(CRound110OffsetGuard const &) = delete;
	~CRound110OffsetGuard()
	{
		delete[] (m_pData - 1);
	}
};

struct CRound110OffsetOwner
{
	int *m_pData;
	CRound110OffsetOwner(CRound110OffsetOwner const &) = delete;
	~CRound110OffsetOwner()
	{
		CRound110OffsetGuard Guard(m_pData);
	}
};

void fg_Round110TakeOffset(CRound110OffsetOwner _Owner)
// CHECK-MESSAGES: :[[@LINE-1]]:49: warning: parameter '_Owner' takes 'CRound110OffsetOwner' by value; take it by 'const &' when the function borrows it or by '&&' when it consumes it [malterlib-parameter-ownership]
{
}

// A helper that makes a deep copy of what it is given releases only the copy.
struct CRound111Owner
{
	CRound111Owner(CRound111Owner const &_Other)
		: m_pData(new int(*_Other.m_pData))
	{
	}
	~CRound111Owner()
	{
		delete m_pData;
	}
	int *m_pData;
};
void fg_Round111Read(CRound111Owner const *_pData)
{
	CRound111Owner Copy(*_pData);
}
struct CRound111View
{
	~CRound111View()
	{
		fg_Round111Read(m_pData);
	}
	CRound111Owner const *m_pData;
};
void fg_Round111Use(CRound111View _View)
{
}

// A result of a template whose coroutine traits are for any of its
// specializations with any parameters, like `TCFuture`, can be a coroutine,
// also when the traits aren't instantiated for the function.
template <typename t_CType>
struct TCRound111Future
{
};
namespace std
{
	template <typename t_CType, typename ...tp_CArgs>
	struct coroutine_traits<TCRound111Future<t_CType>, tp_CArgs...>
	{
		struct promise_type;
	};
}
struct CRound111Interface
{
	virtual TCRound111Future<int> f_Write(CStr _Text) = 0;
};
TCRound111Future<int> fg_Round111Write(CStr _Text)
{
	return {};
}

// A generic lambda is checked as its call operator is instantiated.
struct CRound112Owner
{
	int *m_pData;
	~CRound112Owner()
	{
		delete m_pData;
	}
};
void fg_Round112GenericLambda()
{
	auto fConsume = [](auto _Value)
// CHECK-MESSAGES: :[[@LINE-1]]:26: warning: parameter '_Value' takes 'CRound112Owner' by value; {{.*}}
		{
		};
	fConsume(CRound112Owner{new int});
// CHECK-MESSAGES: :[[@LINE-1]]:10: note: in instantiation of function template specialization 'fg_Round112GenericLambda()::(lambda)::operator()<CRound112Owner>' requested here
	fConsume(1);
}

// A partial specialization of the coroutine traits for any parameters of
// exactly the result makes it a result of coroutines.
struct CRound112Future
{
};
namespace std
{
	template <typename ...tp_CArgs>
	struct coroutine_traits<CRound112Future, tp_CArgs...>
	{
		struct promise_type;
	};
}
CRound112Future fg_Round112Future(CRound112Owner _Owner)
{
	return {};
}

// Round 113: a handle kept complemented is owned
namespace NRound113Complement
{
int close(int _Descriptor);
struct CFile {
  int mp_EncodedDescriptor;
  explicit CFile(int _Descriptor) : mp_EncodedDescriptor(~_Descriptor) {}
  CFile(CFile const &) = delete;
  ~CFile() { if (mp_EncodedDescriptor) close(~mp_EncodedDescriptor); }
};
void fg_Consume(CFile _File) {}
// CHECK-MESSAGES: :[[@LINE-1]]:23: warning: parameter '_File' takes 'CFile' by value; take it by 'const &' when the function borrows it or by '&&' when it consumes it [malterlib-parameter-ownership]
void fg_Call() { fg_Consume(CFile(7)); }
}

// Round 114: a partial specialization of the traits for a pattern of the result
namespace NRound114Traits
{
struct COwner { int *m_pData; ~COwner() { delete m_pData; } };
template<class T> struct TCResult {};
}
namespace std {
  template<class T, class... Args>
  struct coroutine_traits<NRound114Traits::TCResult<T *>, Args...> { struct promise_type; };
}
namespace NRound114Traits {
TCResult<int *> fg_Result(COwner _Value) { return {}; }
}

// Round 114: a discarded write in a helper doesn't make a view owning
namespace NRound114Discarded
{
void fg_Reset(int **_ppData) { if constexpr (false) **_ppData = 0; *_ppData = nullptr; }
struct CView { int *m_pData; ~CView() { fg_Reset(&m_pData); } };
void fg_Borrow(CView _View) {}
}

// Round 115: a partial specialization of the traits whose pattern has values
// matches only results with the same values, and a value it repeats binds once
namespace NRound115Values
{
struct COwner { int *m_pData; ~COwner() { delete m_pData; } };
template<class T, bool B, bool C> struct TCResult {};
template<class T, bool B> struct TCFixed {};
}
namespace std {
  template<class T, bool B, class... Args>
  struct coroutine_traits<NRound115Values::TCResult<T, B, B>, Args...> { struct promise_type; };
  template<class T, class... Args>
  struct coroutine_traits<NRound115Values::TCFixed<T, true>, Args...> { struct promise_type; };
}
namespace NRound115Values {
TCResult<int, true, false> fg_Mismatched(COwner _Value) { return {}; }
// CHECK-MESSAGES: :[[@LINE-1]]:49: warning: parameter '_Value' takes 'COwner' by value; take it by 'const &' when the function borrows it or by '&&' when it consumes it [malterlib-parameter-ownership]
TCResult<int, true, true> fg_Repeated(COwner _Value) { return {}; }
TCFixed<int, true> fg_Fixed(COwner _Value) { return {}; }
TCFixed<int, false> fg_Other(COwner _Value) { return {}; }
// CHECK-MESSAGES: :[[@LINE-1]]:37: warning: parameter '_Value' takes 'COwner' by value; take it by 'const &' when the function borrows it or by '&&' when it consumes it [malterlib-parameter-ownership]
}

// A member function of what a member points to that writes its object releases what is shared.
namespace NRound116PointeeRelease {
struct CCounter {
  unsigned m_nUsers;
  void f_Release() { --m_nUsers; }
};
struct CSubscription {
  CCounter *m_pCounter;
  CSubscription(CSubscription const &_Other)
    : m_pCounter(_Other.m_pCounter) {
    ++m_pCounter->m_nUsers;
  }
  ~CSubscription() { m_pCounter->f_Release(); }
};
void fg_Use(CSubscription _Subscription) {}
// CHECK-MESSAGES: :[[@LINE-1]]:27: warning: parameter '_Subscription' takes 'CSubscription' by value; take it by 'const &' when the function borrows it or by '&&' when it consumes it [malterlib-parameter-ownership]
}

// A cleanup lambda given to a helper that calls it releases what it captures.
namespace NRound117CallbackCleanup {
template <typename tf_F>
void fg_Invoke(tf_F _fWork) { _fWork(); }
struct COwner
{
    int *m_pData;
    COwner(COwner const &) = delete;
    ~COwner() { fg_Invoke([this] { delete m_pData; }); }
};
void fg_Borrow(COwner _Owner) {}
// CHECK-MESSAGES: :[[@LINE-1]]:23: warning: parameter '_Owner' takes 'COwner' by value; take it by 'const &' when the function borrows it or by '&&' when it consumes it [malterlib-parameter-ownership]
}

// A guard given a reference to what is released keeps it.
namespace NRound118ReferenceGuard {
struct CGuard {
  int **m_ppData;
  CGuard(int *&_pData) : m_ppData(&_pData) {}
  ~CGuard() { delete *m_ppData; }
};

struct COwner {
  int *m_pData;
  ~COwner() { CGuard Guard(m_pData); }
};

void fg_Consume(COwner _Owner) {}
// CHECK-MESSAGES: :[[@LINE-1]]:24: warning: parameter '_Owner' takes 'COwner' by value; take it by 'const &' when the function borrows it or by '&&' when it consumes it [malterlib-parameter-ownership]
}

// Round 119: a write in a branch of an `if constexpr` that is discarded
// doesn't make a view owning, and a coroutine trait for a pointer result
// matches it.
namespace NRound119DiscardedWrite {
struct CState {
  unsigned m_nCount;
  void f_Observe() {
    if constexpr (false)
      --m_nCount;
  }
};

struct CView {
  CState *m_pState;
  ~CView() { m_pState->f_Observe(); }
};

void fg_Borrow(CView _View) {}
}

namespace NRound119PointerTraits {
struct COwner {
  int *m_p;
  ~COwner() { delete m_p; }
};

template <typename t_CType> struct TCTask {};
}

namespace std {
template <typename t_CType, typename... tp_CArgs>
struct coroutine_traits<NRound119PointerTraits::TCTask<t_CType> *, tp_CArgs...> {
  struct promise_type;
};
}

namespace NRound119PointerTraits {
TCTask<int> *fg_Task(COwner _Owner) { return nullptr; }
}

// Round 120: a lambda given to a helper that doesn't use it isn't run, and a
// guard that keeps what it is given in a member its destructor doesn't use
// borrows it.
namespace NRound120Borrowed {
template <typename t_FCallback>
void fg_Ignore(t_FCallback const &_fCallback) {}

struct CIgnoring {
  int *m_pData;
  ~CIgnoring() {
    fg_Ignore([this] { delete m_pData; });
  }
};

void fg_Ignoring(CIgnoring _View) {}

struct CGuard {
  int *m_pObserved;
  int *m_pScratch;
  CGuard(int *_pObserved, int *_pScratch) : m_pObserved(_pObserved), m_pScratch(_pScratch) {}
  ~CGuard() { delete m_pScratch; }
};

struct CGuarded {
  int *m_pData;
  ~CGuarded() {
    CGuard Guard(m_pData, new int(0));
  }
};

void fg_Guarded(CGuarded _View) {}
}

// Round 121: a guard given what is owned after it is made releases it.
namespace NRound121
{
struct CGuard {
  int *m_pData = nullptr;
  ~CGuard() { delete m_pData; }
};

struct COwner {
  int *m_pData;
  COwner(COwner const &_Other) : m_pData(new int(*_Other.m_pData)) {}
  ~COwner() {
    CGuard Guard;
    Guard.m_pData = m_pData;
  }
};

void fg_Run(COwner _Owner) {}
// CHECK-MESSAGES: :[[@LINE-1]]:20: warning: parameter '_Owner' takes 'COwner' by value; take it by 'const &' when the function borrows it or by '&&' when it consumes it [malterlib-parameter-ownership]
}

// Round 122: released through an element of a local array assigned it, or
// through a pointer to a member.
namespace NRound122
{
struct CArrayOwner {
  int *m_p;
  CArrayOwner(CArrayOwner const &) = delete;
  ~CArrayOwner() {
    int *Pointers[1];
    Pointers[0] = m_p;
    delete Pointers[0];
  }
};

void fg_Array(CArrayOwner _Owner) {}
// CHECK-MESSAGES: :[[@LINE-1]]:27: warning: parameter '_Owner' takes 'CArrayOwner' by value; take it by 'const &' when the function borrows it or by '&&' when it consumes it [malterlib-parameter-ownership]

struct CMemberOwner {
  int *m_p;
  CMemberOwner(CMemberOwner const &) = delete;
  ~CMemberOwner() {
    int *CMemberOwner::*pmMember = &CMemberOwner::m_p;
    delete (this->*pmMember);
  }
};

void fg_Member(CMemberOwner _Owner) {}
// CHECK-MESSAGES: :[[@LINE-1]]:29: warning: parameter '_Owner' takes 'CMemberOwner' by value; take it by 'const &' when the function borrows it or by '&&' when it consumes it [malterlib-parameter-ownership]
}

// Round 123: an assignment to another element of a local array, a named local
// lambda that is only called, and a member named promise_type that isn't a
// type.
namespace NRound123
{
struct CElement {
  int *m_pData;
  ~CElement() {
    int *Pointers[2] = {new int(*m_pData), nullptr};
    Pointers[1] = m_pData;
    delete Pointers[0];
  }
};

void fg_Element(CElement _View) {}

struct CSameElement {
  int *m_pData;
  ~CSameElement() {
    int *Pointers[2] = {nullptr, nullptr};
    Pointers[0] = m_pData;
    delete Pointers[0];
  }
};

void fg_SameElement(CSameElement _Owner) {}
// CHECK-MESSAGES: :[[@LINE-1]]:34: warning: parameter '_Owner' takes 'CSameElement' by value; take it by 'const &' when the function borrows it or by '&&' when it consumes it [malterlib-parameter-ownership]

struct CNamedLambda {
  int *m_pData;
  ~CNamedLambda() {
    int *pScratch = new int(*m_pData);
    auto fCleanup = [pScratch] { delete pScratch; };
    fCleanup();
  }
};

void fg_NamedLambda(CNamedLambda _View) {}

struct CKeptLambda {
  int *m_pData;
  ~CKeptLambda() {
    int *pData = m_pData;
    auto fCleanup = [pData] { delete pData; };
    fCleanup();
  }
};

void fg_KeptLambda(CKeptLambda _Owner) {}
// CHECK-MESSAGES: :[[@LINE-1]]:32: warning: parameter '_Owner' takes 'CKeptLambda' by value; take it by 'const &' when the function borrows it or by '&&' when it consumes it [malterlib-parameter-ownership]

struct COwner {
  int *m_pData;
  ~COwner() { delete m_pData; }
};

struct CNotPromise {
  static int promise_type;
};

CNotPromise fg_NotPromise(COwner _Owner) { return {}; }
// CHECK-MESSAGES: :[[@LINE-1]]:34: warning: parameter '_Owner' takes 'COwner' by value; take it by 'const &' when the function borrows it or by '&&' when it consumes it [malterlib-parameter-ownership]
}

// Round 124: a partial specialization of the coroutine traits for some given
// parameters and then any can make the function a coroutine.
namespace NRound124
{
struct COwner {
  int *m_p;
  ~COwner() { delete m_p; }
};
struct CTask {};
struct CPromise {
  CTask get_return_object();
  std::suspend_never initial_suspend();
  std::suspend_never final_suspend() noexcept;
  void return_void();
  void unhandled_exception();
};
struct COtherTask {};
}

namespace std
{
	template <typename ...tp_CArgs>
	struct coroutine_traits<NRound124::CTask, NRound124::COwner, tp_CArgs...>
	{
		using promise_type = NRound124::CPromise;
	};

	template <typename ...tp_CArgs>
	struct coroutine_traits<NRound124::COtherTask, int, tp_CArgs...>
	{
		using promise_type = NRound124::CPromise;
	};
}

namespace NRound124
{
CTask fg_Task(COwner _Owner) { return {}; }

CTask fg_TaskMore(COwner _Owner, int _Value) { return {}; }

COtherTask fg_OtherTask(COwner _Owner) { return {}; }
// CHECK-MESSAGES: :[[@LINE-1]]:32: warning: parameter '_Owner' takes 'COwner' by value; take it by 'const &' when the function borrows it or by '&&' when it consumes it [malterlib-parameter-ownership]
}

// Round 125: a guard that releases what a global has through a member
// function of it, or an atomic count of a global, owns it, also through a
// chain of helpers.
namespace NRound125
{
struct CResource {
  int *m_pData;
  void f_Release() {
    delete m_pData;
    m_pData = nullptr;
  }
  int f_Peek() const { return *m_pData; }
};
CResource g_Resource;

struct CMemberGuard {
  CMemberGuard() = default;
  CMemberGuard(CMemberGuard const &_Other) = delete;
  ~CMemberGuard() { g_Resource.f_Release(); }
};

void fg_Member(CMemberGuard _Guard) {}
// CHECK-MESSAGES: :[[@LINE-1]]:29: warning: parameter '_Guard' takes 'CMemberGuard' by value; take it by 'const &' when the function borrows it or by '&&' when it consumes it [malterlib-parameter-ownership]

struct CPeekGuard {
  CPeekGuard() = default;
  CPeekGuard(CPeekGuard const &_Other) = delete;
  ~CPeekGuard() { (void)g_Resource.f_Peek(); }
};

void fg_Peek(CPeekGuard _Guard) {}

unsigned g_nUsers;

struct CAtomicGuard {
  CAtomicGuard() = default;
  CAtomicGuard(CAtomicGuard const &_Other) = delete;
  ~CAtomicGuard() { __atomic_fetch_sub(&g_nUsers, 1u, __ATOMIC_RELAXED); }
};

void fg_Atomic(CAtomicGuard _Guard) {}
// CHECK-MESSAGES: :[[@LINE-1]]:29: warning: parameter '_Guard' takes 'CAtomicGuard' by value; take it by 'const &' when the function borrows it or by '&&' when it consumes it [malterlib-parameter-ownership]

int *g_pResource;
void fg_Release() { delete g_pResource; }
void fg_Close() { fg_Release(); }
void fg_Cleanup() { fg_Close(); }

struct CChainGuard {
  CChainGuard() = default;
  CChainGuard(CChainGuard const &_Other) = delete;
  ~CChainGuard() { fg_Cleanup(); }
};

void fg_Chain(CChainGuard _Guard) {}
// CHECK-MESSAGES: :[[@LINE-1]]:27: warning: parameter '_Guard' takes 'CChainGuard' by value; take it by 'const &' when the function borrows it or by '&&' when it consumes it [malterlib-parameter-ownership]
}

// Round 126: a helper that returns what releases releases.
namespace NRound126
{
unsigned g_nLocks;
void fg_Unlock() { --g_nLocks; }
void fg_Release() { return fg_Unlock(); }

struct CGuard {
  CGuard() { ++g_nLocks; }
  CGuard(CGuard const &_Other) = delete;
  ~CGuard() { fg_Release(); }
};

void fg_Process(CGuard _Guard) {}
// CHECK-MESSAGES: :[[@LINE-1]]:24: warning: parameter '_Guard' takes 'CGuard' by value; take it by 'const &' when the function borrows it or by '&&' when it consumes it [malterlib-parameter-ownership]
}

// Round 127: a release in an argument of another call releases.
namespace NRound127
{
unsigned g_nLocks;
void fg_Unlock() { --g_nLocks; }
void fg_Work(int _Value) {}

struct CGuard {
  CGuard() = default;
  CGuard(CGuard const &_Other) = delete;
  ~CGuard() { fg_Work((fg_Unlock(), 0)); }
};

void fg_Take(CGuard _Guard) {}
// CHECK-MESSAGES: :[[@LINE-1]]:21: warning: parameter '_Guard' takes 'CGuard' by value; take it by 'const &' when the function borrows it or by '&&' when it consumes it [malterlib-parameter-ownership]
}

// Round 128: a local that each branch before points at what it allocates
// isn't of the object, and a move that isn't evaluated doesn't consume.
namespace NRound128
{
struct CView {
  int *m_pData;
  bool m_bChoice;

  ~CView() {
    int *pScratch = m_pData;
    if (m_bChoice)
      pScratch = new int(1);
    else
      pScratch = new int(2);
    delete pScratch;
  }
};

void fg_UseView(CView _View) {}

struct COwner {
  int *m_pData;
  ~COwner() { delete m_pData; }
};

void fg_Read(COwner const &_Owner);

void fg_Use(COwner _Owner) {
// CHECK-MESSAGES: :[[@LINE-1]]:20: warning: parameter '_Owner' takes 'COwner' by value; take it by 'const &' when the function borrows it or by '&&' when it consumes it [malterlib-parameter-ownership]
  static_assert(noexcept(std::move(_Owner)));
  fg_Read(_Owner);
}
}

// Round 129: calling a lambda's call operator by name runs its body.
namespace NRound129
{
struct COwner {
  int *m_pData;
  explicit COwner(int *_pData) : m_pData(_pData) {}
  COwner(COwner const &_Other) = delete;

  ~COwner() {
    auto fRelease = [this] { delete m_pData; };
    fRelease.operator()();
  }
};

void fg_Run(COwner _Value) {}
// CHECK-MESSAGES: :[[@LINE-1]]:20: warning: parameter '_Value' takes 'COwner' by value; take it by 'const &' when the function borrows it or by '&&' when it consumes it [malterlib-parameter-ownership]
}

// Round 130: a local copy of a value given by reference is its own storage.
namespace NRound130
{
struct CState {
  int m_Value;
};
void fg_Normalize(CState &_State) { _State.m_Value = 0; }

struct CView {
  CState const *m_pState;
  ~CView() {
    CState State = *m_pState;
    fg_Normalize(State);
  }
};

void fg_Read(CView _View) {}
}

// Round 131: what a local array points to is its first element.
namespace NRound131
{
struct COwner {
  COwner(COwner const &_Other) = delete;
  int *m_pData;
  ~COwner() {
    int *Pointers[]{m_pData};
    delete *Pointers;
  }
};

void fg_Borrow(COwner _Owner) {}
// CHECK-MESSAGES: :[[@LINE-1]]:23: warning: parameter '_Owner' takes 'COwner' by value; take it by 'const &' when the function borrows it or by '&&' when it consumes it [malterlib-parameter-ownership]
}

// Round 132: a count a reference member refers to isn't in the object, also
// changed atomically.
namespace NRound132
{
struct CToken {
  unsigned &m_nUsers;
  explicit CToken(unsigned &_nUsers) : m_nUsers(_nUsers) { ++m_nUsers; }
  CToken(CToken const &_Other) : m_nUsers(_Other.m_nUsers) { ++m_nUsers; }
  ~CToken() { __atomic_fetch_sub(&m_nUsers, 1u, __ATOMIC_RELAXED); }
};

void fg_Borrow(CToken _Token) {}
// CHECK-MESSAGES: :[[@LINE-1]]:23: warning: parameter '_Token' takes 'CToken' by value; take it by 'const &' when the function borrows it or by '&&' when it consumes it [malterlib-parameter-ownership]
}

// Round 133: a guard a member function returns of the object releases, and
// one given it in a member its destructor doesn't use doesn't.
namespace NRound133
{
struct CGuard {
  int *m_pData;
  ~CGuard() { delete m_pData; }
};

struct COwner {
  int *m_pData;
  explicit COwner(int *_pData) : m_pData(_pData) {}
  COwner(COwner const &_Other) = delete;
  CGuard f_Release() { return {m_pData}; }
  ~COwner() { f_Release(); }
};

void fg_Owner(COwner _Owner) {}
// CHECK-MESSAGES: :[[@LINE-1]]:22: warning: parameter '_Owner' takes 'COwner' by value; take it by 'const &' when the function borrows it or by '&&' when it consumes it [malterlib-parameter-ownership]

struct CScratchGuard {
  int *m_pObserved = nullptr;
  int *m_pScratch = new int(0);
  ~CScratchGuard() { delete m_pScratch; }
};

struct CView {
  int *m_pData;
  ~CView() {
    CScratchGuard Guard;
    Guard.m_pObserved = m_pData;
  }
};

void fg_View(CView _View) {}
}

// Round 134: only the actor of Malterlib, not another class of its name, may
// take owning parameters by value.
namespace NRound134
{
struct COwner {
  int *m_pData;
  ~COwner() { delete m_pData; }
};

namespace NMovie
{
struct CActor {
  void f_Set(COwner _Value) {}
// CHECK-MESSAGES: :[[@LINE-1]]:21: warning: parameter '_Value' takes 'COwner' by value; take it by 'const &' when the function borrows it or by '&&' when it consumes it [malterlib-parameter-ownership]
};
}
}

// Round 135: a temporary guard that keeps what it is given only in a member
// its destructor doesn't use doesn't release it, and a pointer moved where it
// is, `--m_pData`, is it.
namespace NRound135
{
struct CGuard {
  int *m_pObserved;
  int *m_pScratch;
  CGuard(int *_pObserved, int *_pScratch)
      : m_pObserved(_pObserved), m_pScratch(_pScratch) {}
  ~CGuard() { delete m_pScratch; }
};

struct CView {
  int *m_pData;
  ~CView() { (void)CGuard{m_pData, new int(1)}; }
};

void fg_View(CView _View) {}

struct COwner {
  int *m_pData;
  COwner() : m_pData(new int[2] + 1) {}
  COwner(COwner const &_Other) = delete;
  ~COwner() { delete[] --m_pData; }
};

void fg_Owner(COwner _Owner) {}
// CHECK-MESSAGES: :[[@LINE-1]]:22: warning: parameter '_Owner' takes 'COwner' by value; take it by 'const &' when the function borrows it or by '&&' when it consumes it [malterlib-parameter-ownership]
}

// Round 136: an element assigned itself is what it was; one replaced before
// it is read is what replaced it; and a member of a guard that releases by
// itself is used.
namespace NRound136
{
struct CSelf {
  int *m_pData;
  ~CSelf() {
    int *Pointers[] = {m_pData};
    Pointers[0] = Pointers[0];
    delete Pointers[0];
  }
};

void fg_Self(CSelf _Self) {}
// CHECK-MESSAGES: :[[@LINE-1]]:20: warning: parameter '_Self' takes 'CSelf' by value; take it by 'const &' when the function borrows it or by '&&' when it consumes it [malterlib-parameter-ownership]

struct CReplaced {
  int *m_pData;
  ~CReplaced() {
    int *Pointers[] = {m_pData};
    Pointers[0] = new int(*Pointers[0]);
    delete Pointers[0];
  }
};

void fg_Replaced(CReplaced _View) {}

struct CRelease {
  int *m_pData;
  ~CRelease() { delete m_pData; }
};

struct CGuard {
  CRelease m_Release;
  CGuard(int *_pData) : m_Release{_pData} {}
  ~CGuard() {}
};

struct COwner {
  int *m_pData;
  ~COwner() { CGuard Guard(m_pData); }
};

void fg_Owner(COwner _Owner) {}
// CHECK-MESSAGES: :[[@LINE-1]]:22: warning: parameter '_Owner' takes 'COwner' by value; take it by 'const &' when the function borrows it or by '&&' when it consumes it [malterlib-parameter-ownership]
}

namespace NRound137 {
// An element of an array that is replaced, `Pointers[0] = new
// int(*Pointers[0]);`, no longer has what it was assigned before.
struct CView {
  int *m_pData;
  ~CView() {
    int *Pointers[1];
    Pointers[0] = m_pData;
    Pointers[0] = new int(*Pointers[0]);
    delete Pointers[0];
  }
};

void fg_View(CView _View) {}

// But it has what replaces it.
struct COwner {
  int *m_pData;
  ~COwner() {
    int *Pointers[1];
    Pointers[0] = new int(1);
    Pointers[0] = m_pData;
    delete Pointers[0];
  }
};

void fg_Owner(COwner _Owner) {}
// CHECK-MESSAGES: :[[@LINE-1]]:22: warning: parameter '_Owner' takes 'COwner' by value; take it by 'const &' when the function borrows it or by '&&' when it consumes it [malterlib-parameter-ownership]

// A check of a view that fails, `if (!m_pData) fg_Fail();`, also of one with
// a copy constructor of its own.
[[noreturn]] void fg_Fail();

struct CChecked {
  int *m_pData;
  CChecked(CChecked const &_Other) : m_pData(_Other.m_pData) {}
  ~CChecked() {
    if (!m_pData)
      fg_Fail();
  }
};

void fg_Checked(CChecked _Checked) {}
}

namespace NRound138 {
// A callback that a helper only names where it isn't evaluated,
// `noexcept(_fWork())`, isn't run.
template <typename tf_FWork>
void fg_Query(tf_FWork const &_fWork) { (void)noexcept(_fWork()); }

struct CQueried {
  int *m_pData;
  ~CQueried() { fg_Query([this] { delete m_pData; }); }
};

void fg_Queried(CQueried _Queried) {}

// But one it calls is.
template <typename tf_FWork>
void fg_Invoke(tf_FWork const &_fWork) { _fWork(); }

struct CInvoked {
  int *m_pData;
  ~CInvoked() { fg_Invoke([this] { delete m_pData; }); }
};

void fg_Invoked(CInvoked _Invoked) {}
// CHECK-MESSAGES: :[[@LINE-1]]:26: warning: parameter '_Invoked' takes 'CInvoked' by value; take it by 'const &' when the function borrows it or by '&&' when it consumes it [malterlib-parameter-ownership]

// What an element of an array is assigned after it is read doesn't change
// what was read.
void fg_Log(int const *_pData);

struct CScratch {
  int *m_pData;
  ~CScratch() {
    int *Pointers[] = {new int(*m_pData)};
    fg_Log(Pointers[0]);
    delete Pointers[0];
    Pointers[0] = m_pData;
    fg_Log(Pointers[0]);
  }
};

void fg_Scratch(CScratch _Scratch) {}

// Unless a loop around the read runs it again after.
struct CLooped {
  int *m_pData;
  ~CLooped() {
    int *Pointers[] = {new int(*m_pData)};
    for (int iTime = 0; iTime < 2; ++iTime) {
      delete Pointers[0];
      Pointers[0] = m_pData;
    }
  }
};

void fg_Looped(CLooped _Looped) {}
// CHECK-MESSAGES: :[[@LINE-1]]:24: warning: parameter '_Looped' takes 'CLooped' by value; take it by 'const &' when the function borrows it or by '&&' when it consumes it [malterlib-parameter-ownership]
}

namespace NRound139 {
// A helper that calls a lambda that releases it by name,
// `fRelease.operator()()`.
void fg_Release(int *_pData) {
  auto fRelease = [_pData] { delete _pData; };
  fRelease.operator()();
}

struct COwner {
  int *m_pData;
  ~COwner() { fg_Release(m_pData); }
};

void fg_Owner(COwner _Owner) {}
// CHECK-MESSAGES: :[[@LINE-1]]:22: warning: parameter '_Owner' takes 'COwner' by value; take it by 'const &' when the function borrows it or by '&&' when it consumes it [malterlib-parameter-ownership]

// But not a helper that deletes a copy that another makes, `delete fg_Copy(
// _pData)`.
int *fg_Copy(int const *_pData) { return new int(*_pData); }
void fg_DiscardScratch(int const *_pData) { delete fg_Copy(_pData); }

struct CView {
  int const *m_pData;
  ~CView() { fg_DiscardScratch(m_pData); }
};

void fg_View(CView _View) {}
}

namespace NRound141 {
// A defaulted comparison, which can't be a coroutine.
struct COwner {
  int *m_pData;
  COwner(COwner const &_Other);
  ~COwner() { delete m_pData; }
  friend bool operator==(COwner _First, COwner _Second) = default;
// CHECK-MESSAGES: :[[@LINE-1]]:33: warning: parameter '_First' takes 'COwner' by value; take it by 'const &' when the function borrows it or by '&&' when it consumes it [malterlib-parameter-ownership]
// CHECK-MESSAGES: :[[@LINE-2]]:48: warning: parameter '_Second' takes 'COwner' by value; take it by 'const &' when the function borrows it or by '&&' when it consumes it [malterlib-parameter-ownership]
};

bool fg_Equal(COwner const &_First, COwner const &_Second) {
  return _First == _Second;
}

// A lambda that releases a scratch copy, called through a copy of it, `auto
// fCopy = fCleanup; fCopy();`.
struct CView {
  int const *m_pData;
  ~CView() {
    auto *pScratch = new int(*m_pData);
    auto fCleanup = [pScratch] { delete pScratch; };
    auto fCopy = fCleanup;
    fCopy();
  }
};

void fg_View(CView _View) {}
}

namespace NRound142 {
// An owning member of a template whose destructor isn't instantiated.
struct COwner {
  int *m_pData;
  ~COwner() { delete m_pData; }
};

template <typename t_CType>
struct TCWrapper {
  COwner m_Owner;
  ~TCWrapper() {}
};

void fg_Wrapped(TCWrapper<int> _Value) {}
// CHECK-MESSAGES: :[[@LINE-1]]:32: warning: parameter '_Value' takes 'TCWrapper<int>' by value; take it by 'const &' when the function borrows it or by '&&' when it consumes it [malterlib-parameter-ownership]
}

namespace NRound143 {
// What the first element is assigned before it is read through the array,
// `delete *Pointers`.
struct COwner {
  int *m_pData;
  ~COwner() {
    int *Pointers[] = {nullptr};
    Pointers[0] = m_pData;
    delete *Pointers;
  }
};

void fg_Owner(COwner _Owner) {}
// CHECK-MESSAGES: :[[@LINE-1]]:22: warning: parameter '_Owner' takes 'COwner' by value; take it by 'const &' when the function borrows it or by '&&' when it consumes it [malterlib-parameter-ownership]

struct CView {
  int *m_pData;
  ~CView() {
    int *Pointers[] = {m_pData};
    Pointers[0] = new int(*m_pData);
    delete *Pointers;
  }
};

void fg_View(CView _View) {}
}

namespace NRound144 {
// A helper that only makes a lambda of the callback, which isn't called.
template <typename tf_FWork>
auto fg_Deferred(tf_FWork const &_fWork) {
  return [_fWork] { _fWork(); };
}

struct CView {
  int *m_pData;
  ~CView() { auto fDeferred = fg_Deferred([this] { delete m_pData; }); }
};

void fg_View(CView _View) {}

// But one whose result is called.
struct COwner {
  int *m_pData;
  ~COwner() {
    auto fDeferred = fg_Deferred([this] { delete m_pData; });
    fDeferred();
  }
};

void fg_Owner(COwner _Owner) {}
// CHECK-MESSAGES: :[[@LINE-1]]:22: warning: parameter '_Owner' takes 'COwner' by value; take it by 'const &' when the function borrows it or by '&&' when it consumes it [malterlib-parameter-ownership]

struct CCalled {
  int *m_pData;
  ~CCalled() { fg_Deferred([this] { delete m_pData; })(); }
};

void fg_Called(CCalled _Called) {}
// CHECK-MESSAGES: :[[@LINE-1]]:24: warning: parameter '_Called' takes 'CCalled' by value; take it by 'const &' when the function borrows it or by '&&' when it consumes it [malterlib-parameter-ownership]
}

// A specialization of the coroutine traits for exactly what a function is
// given, without a promise type, is what its traits are.
namespace NRound145 {
struct COwner {
  int *m_pData;
  ~COwner() { delete m_pData; }
};
struct CTask {};
}
namespace std {
template <typename... tp_CArgs>
struct coroutine_traits<NRound145::CTask, tp_CArgs...> {
  struct promise_type {};
};
template <>
struct coroutine_traits<NRound145::CTask, NRound145::COwner> {};
}
namespace NRound145 {
CTask fg_Selected(COwner _Value) { return {}; }
// CHECK-MESSAGES: :[[@LINE-1]]:26: warning: parameter '_Value' takes 'COwner' by value; take it by 'const &' when the function borrows it or by '&&' when it consumes it [malterlib-parameter-ownership]
}

namespace NRound146 {
template <typename t_FCleanup>
struct TCGuard
{
	t_FCleanup m_fCleanup;
	~TCGuard()
	{
		m_fCleanup();
	}
};

// A cleanup lambda held in a local, given to a guard.
struct COwner
{
	int *m_pData;
	explicit COwner(int *_pData) : m_pData(_pData) {}
	COwner(COwner const &) = delete;
	~COwner()
	{
		auto fCleanup = [this] { delete m_pData; };
		TCGuard Guard{fCleanup};
	}
};

void fg_Run(COwner _Owner) {}
// CHECK-MESSAGES: :[[@LINE-1]]:20: warning: parameter '_Owner' takes 'COwner' by value; take it by 'const &' when the function borrows it or by '&&' when it consumes it [malterlib-parameter-ownership]
void fg_Use()
{
	fg_Run(COwner(new int));
}
}

namespace NRound147 {
// Released by a guard through a member function of it.
struct CGuard
{
	int *m_pData;
	CGuard(int *_pData) : m_pData(_pData) {}
	void f_Clear()
	{
		delete m_pData;
	}
	~CGuard()
	{
		f_Clear();
	}
};

struct COwner
{
	int *m_pData;
	COwner(COwner const &_Other);
	~COwner()
	{
		CGuard Guard(m_pData);
	}
};

void fg_Borrow(COwner _Value) {}
// CHECK-MESSAGES: :[[@LINE-1]]:23: warning: parameter '_Value' takes 'COwner' by value; take it by 'const &' when the function borrows it or by '&&' when it consumes it [malterlib-parameter-ownership]

// A defaulted comparison that nothing uses.
struct CCompared
{
	int *m_pData;
	CCompared(CCompared const &_Other);
	~CCompared()
	{
		delete m_pData;
	}
	friend bool operator==(CCompared _First, CCompared _Second) = default;
// CHECK-MESSAGES: :[[@LINE-1]]:35: warning: parameter '_First' takes 'CCompared' by value; take it by 'const &' when the function borrows it or by '&&' when it consumes it [malterlib-parameter-ownership]
// CHECK-MESSAGES: :[[@LINE-2]]:53: warning: parameter '_Second' takes 'CCompared' by value; take it by 'const &' when the function borrows it or by '&&' when it consumes it [malterlib-parameter-ownership]
};
}

namespace NRound148 {
int *g_pResource;
void fg_Release()
{
	delete g_pResource;
}
void fg_Work(int _Unused = (fg_Release(), 0)) {}

// A release with an attribute.
struct CAttributed
{
	CAttributed() = default;
	CAttributed(CAttributed const &) = delete;
	~CAttributed()
	{
		[[likely]] fg_Release();
	}
};
void fg_ProcessAttributed(CAttributed _Guard) {}
// CHECK-MESSAGES: :[[@LINE-1]]:39: warning: parameter '_Guard' takes 'CAttributed' by value; take it by 'const &' when the function borrows it or by '&&' when it consumes it [malterlib-parameter-ownership]

// A release in a default argument of a call.
struct CDefaulted
{
	CDefaulted() = default;
	CDefaulted(CDefaulted const &) = delete;
	~CDefaulted()
	{
		fg_Work();
	}
};
void fg_ProcessDefaulted(CDefaulted _Guard) {}
// CHECK-MESSAGES: :[[@LINE-1]]:37: warning: parameter '_Guard' takes 'CDefaulted' by value; take it by 'const &' when the function borrows it or by '&&' when it consumes it [malterlib-parameter-ownership]
}

namespace NRound151 {
unsigned g_nLocks;

// A count released by a lambda called where it is made, and in a condition.
struct CLambdaGuard
{
	CLambdaGuard()
	{
		++g_nLocks;
	}
	CLambdaGuard(CLambdaGuard const &) = delete;
	~CLambdaGuard()
	{
		[] { --g_nLocks; }();
	}
};
void fg_RunLambda(CLambdaGuard _Guard) {}
// CHECK-MESSAGES: :[[@LINE-1]]:32: warning: parameter '_Guard' takes 'CLambdaGuard' by value; take it by 'const &' when the function borrows it or by '&&' when it consumes it [malterlib-parameter-ownership]

struct CConditionGuard
{
	CConditionGuard()
	{
		++g_nLocks;
	}
	CConditionGuard(CConditionGuard const &) = delete;
	~CConditionGuard()
	{
		if (--g_nLocks == 0)
		{
		}
	}
};
void fg_RunCondition(CConditionGuard _Guard) {}
// CHECK-MESSAGES: :[[@LINE-1]]:38: warning: parameter '_Guard' takes 'CConditionGuard' by value; take it by 'const &' when the function borrows it or by '&&' when it consumes it [malterlib-parameter-ownership]
}

namespace NRound152 {
// Released through an array made empty, assigned after.
struct CEmptyArray
{
	int *m_pData;
	CEmptyArray(CEmptyArray const &_Other) = delete;
	~CEmptyArray()
	{
		int *Pointers[1] = {};
		Pointers[0] = m_pData;
		delete *Pointers;
	}
};
void fg_EmptyArray(CEmptyArray _Owner) {}
// CHECK-MESSAGES: :[[@LINE-1]]:32: warning: parameter '_Owner' takes 'CEmptyArray' by value; take it by 'const &' when the function borrows it or by '&&' when it consumes it [malterlib-parameter-ownership]

// A member of its own written through a local pointer to the object.
struct CView
{
	int *m_pBorrowed;
	~CView()
	{
		auto pSelf = this;
		pSelf->m_pBorrowed = nullptr;
	}
};
void fg_View(CView _View) {}

// An array member of its own written through a stepped pointer.
struct CCache
{
	int m_Cache[2];
	~CCache()
	{
		int *pEnd = m_Cache + 2;
		*--pEnd = 0;
	}
};
void fg_Cache(CCache _Cache) {}
}

namespace NRound153 {
// A local made from itself in a destructor.
struct COwner {
  int *m_pData;
  ~COwner() {
    int *pData = false ? pData : m_pData;
    delete pData;
  }
};
void fg_Take(COwner _Value) {}
// CHECK-MESSAGES: :[[@LINE-1]]:21: warning: parameter '_Value' takes 'COwner' by value; take it by 'const &' when the function borrows it or by '&&' when it consumes it [malterlib-parameter-ownership]
}

namespace NRound154 {
// Cleanup on the right of `&&` is like in a branch.
void fg_Unlock();
struct CGuard {
  bool m_bActive;
  explicit CGuard(bool _bActive) : m_bActive(_bActive) {}
  CGuard(CGuard const &) = delete;
  ~CGuard() { (void)(m_bActive && (fg_Unlock(), true)); }
};
void fg_Use(CGuard _Guard) {}
// CHECK-MESSAGES: :[[@LINE-1]]:20: warning: parameter '_Guard' takes 'CGuard' by value; take it by 'const &' when the function borrows it or by '&&' when it consumes it [malterlib-parameter-ownership]
void fg_Call() { fg_Use(CGuard(true)); }
}

namespace NRound155 {
// A destructor that only points a local copy elsewhere.
void fg_Reset(int *&_pData) { _pData = nullptr; }
struct CView {
  int *m_pData;
  ~CView() {
    auto pCopy = m_pData;
    fg_Reset(pCopy);
  }
};
void fg_Process(CView _View) {}

// What isn't evaluated doesn't release.
struct CUnevaluated {
  int *m_pData;
  ~CUnevaluated() {
    (void)__builtin_constant_p((delete m_pData, 0));
    (void)__builtin_choose_expr(1, 0, (delete m_pData, 0));
  }
};
void fg_Unevaluated(CUnevaluated _View) {}
}

namespace NRound156 {
// Released through a helper that returns its member.
struct COwner {
  int *m_pData;
  explicit COwner(int *_pData) : m_pData(_pData) {}
  COwner(COwner const &) = delete;
  static int *fs_Data(COwner const &_Owner) { return _Owner.m_pData; }
  ~COwner() { delete fs_Data(*this); }
};
void fg_Borrow(COwner _Owner) {}
// CHECK-MESSAGES: :[[@LINE-1]]:23: warning: parameter '_Owner' takes 'COwner' by value; take it by 'const &' when the function borrows it or by '&&' when it consumes it [malterlib-parameter-ownership]
}

namespace NRound157 {
// Given to a local guard through a setter.
struct CReleaseGuard {
  int *m_pData = nullptr;
  void f_Set(int *_pData) { m_pData = _pData; }
  ~CReleaseGuard() { delete m_pData; }
};
struct COwner {
  int *m_pData;
  COwner(COwner const &) = delete;
  ~COwner() {
    CReleaseGuard Guard;
    Guard.f_Set(m_pData);
  }
};
void fg_Set(COwner _Owner) {}
// CHECK-MESSAGES: :[[@LINE-1]]:20: warning: parameter '_Owner' takes 'COwner' by value; take it by 'const &' when the function borrows it or by '&&' when it consumes it [malterlib-parameter-ownership]

// A guard whose destructor releases through a member function.
struct CClearGuard {
  int *m_pData = nullptr;
  void f_Clear() { delete m_pData; }
  ~CClearGuard() { f_Clear(); }
};
struct CDelegating {
  int *m_pData;
  CDelegating(CDelegating const &) = delete;
  ~CDelegating() {
    CClearGuard Guard;
    Guard.m_pData = m_pData;
  }
};
void fg_Delegating(CDelegating _Owner) {}
// CHECK-MESSAGES: :[[@LINE-1]]:32: warning: parameter '_Owner' takes 'CDelegating' by value; take it by 'const &' when the function borrows it or by '&&' when it consumes it [malterlib-parameter-ownership]

// Released through a local pointer to a function.
unsigned g_nLocks;
void fg_Unlock() { --g_nLocks; }
struct CLockGuard {
  CLockGuard() { ++g_nLocks; }
  CLockGuard(CLockGuard const &) = delete;
  ~CLockGuard() {
    auto fUnlock = &fg_Unlock;
    fUnlock();
  }
};
void fg_Locked(CLockGuard _Guard) {}
// CHECK-MESSAGES: :[[@LINE-1]]:27: warning: parameter '_Guard' takes 'CLockGuard' by value; take it by 'const &' when the function borrows it or by '&&' when it consumes it [malterlib-parameter-ownership]
}

namespace NRound158 {
// A virtual destructor called explicitly, which runs the override.
struct ICResource {
  virtual ~ICResource() {}
};
struct COwner {
  ICResource *m_pResource;
  COwner(ICResource *_pResource) : m_pResource(_pResource) {}
  COwner(COwner const &) = delete;
  ~COwner() { m_pResource->~ICResource(); }
};
void fg_Consume(COwner _Owner) {}
// CHECK-MESSAGES: :[[@LINE-1]]:24: warning: parameter '_Owner' takes 'COwner' by value; take it by 'const &' when the function borrows it or by '&&' when it consumes it [malterlib-parameter-ownership]
}

namespace NRound159 {
// Released through what a lambda returns.
struct COwner {
  int *m_pData;
  COwner(COwner const &) = delete;
  COwner(COwner &&);
  ~COwner() { delete [this] { return m_pData; }(); }
};
void fg_Use(COwner _Value) {}
// CHECK-MESSAGES: :[[@LINE-1]]:20: warning: parameter '_Value' takes 'COwner' by value; take it by 'const &' when the function borrows it or by '&&' when it consumes it [malterlib-parameter-ownership]

// Only a scratch copy put first in a local array is deleted.
struct CView {
  int *m_pData;
  ~CView() {
    int *Pointers[] = {m_pData};
    *Pointers = new int(**Pointers);
    delete *Pointers;
  }
};
void fg_View(CView _View) {}
}

namespace NRound161 {
// Writing its own member through a pointer to it.
struct CState {
  unsigned m_Value;
};
struct CView {
  CState m_State;
  ~CView() {
    auto pState = &m_State;
    pState->m_Value = 0;
  }
};
void fg_Test(CView _View) {}
}

namespace NRound162 {
// A helper that points its parameter at a scratch copy and deletes that.
void fg_Scratch(int *_pData) {
  _pData = new int(*_pData);
  delete _pData;
}
struct CView {
  int *m_pData;
  ~CView() { fg_Scratch(m_pData); }
};
void fg_Use(CView _View) {}
}

namespace NRound163 {
// Moved from a copy a lambda captures, not from the parameter.
struct COwner {
  int *m_pData;
  COwner(COwner const &_Other);
  ~COwner() { delete m_pData; }
};
void fg_Consume(COwner &&_Value);
auto fg_Make(COwner _Value) {
// CHECK-MESSAGES: :[[@LINE-1]]:21: warning: parameter '_Value' takes 'COwner' by value; take it by 'const &' when the function borrows it or by '&&' when it consumes it [malterlib-parameter-ownership]
  return [_Value]() mutable { fg_Consume(static_cast<COwner &&>(_Value)); };
}
}

namespace NRound164 {
// Ownership given to a local through a helper's output parameter, or a swap.
void fg_Take(int *&o_pData, int *&_pData) {
  o_pData = _pData;
  _pData = nullptr;
}
struct CTaken {
  int *m_pData;
  explicit CTaken(int *_pData) : m_pData(_pData) {}
  CTaken(CTaken const &) = delete;
  ~CTaken() {
    int *pData = nullptr;
    fg_Take(pData, m_pData);
    delete pData;
  }
};
void fg_UseTaken(CTaken _Owner) {}
// CHECK-MESSAGES: :[[@LINE-1]]:25: warning: parameter '_Owner' takes 'CTaken' by value; take it by 'const &' when the function borrows it or by '&&' when it consumes it [malterlib-parameter-ownership]
template <typename t_CType> void fg_Swap(t_CType &_First, t_CType &_Second) {
  t_CType Temporary = static_cast<t_CType &&>(_First);
  _First = static_cast<t_CType &&>(_Second);
  _Second = static_cast<t_CType &&>(Temporary);
}
struct CSwapped {
  int *m_pData;
  explicit CSwapped(int *_pData) : m_pData(_pData) {}
  CSwapped(CSwapped const &) = delete;
  ~CSwapped() {
    int *pData = nullptr;
    fg_Swap(pData, m_pData);
    delete pData;
  }
};
void fg_UseSwapped(CSwapped _Owner) {}
// CHECK-MESSAGES: :[[@LINE-1]]:29: warning: parameter '_Owner' takes 'CSwapped' by value; take it by 'const &' when the function borrows it or by '&&' when it consumes it [malterlib-parameter-ownership]
// Not when the helper doesn't give it.
void fg_Clear(int *&o_pData, int *&_pData) { o_pData = nullptr; }
struct CCleared {
  int *m_pData;
  explicit CCleared(int *_pData) : m_pData(_pData) {}
  CCleared(CCleared const &) = delete;
  ~CCleared() {
    int *pData = nullptr;
    fg_Clear(pData, m_pData);
    delete pData;
  }
};
void fg_UseCleared(CCleared _Owner) {}
}

namespace NRound165 {
// Deleted through a range loop over a local array.
struct COwner {
  int *m_pData;
  ~COwner() {
    int *Pointers[] = {m_pData};
    for (int *pData : Pointers)
      delete pData;
  }
};
void fg_Borrow(COwner _Owner) {}
// CHECK-MESSAGES: :[[@LINE-1]]:23: warning: parameter '_Owner' takes 'COwner' by value; take it by 'const &' when the function borrows it or by '&&' when it consumes it [malterlib-parameter-ownership]
}

namespace NRound166 {
// Not given what it deletes in a branch of an `if constexpr` that is
// discarded.
void fg_Take(int *&o_pValue, int *_pOther) { o_pValue = _pOther; }
struct CView {
  int *m_pBorrowed;
  ~CView() {
    int *pScratch = nullptr;
    if constexpr (false)
      fg_Take(pScratch, m_pBorrowed);
    delete pScratch;
  }
};
void fg_Visit(CView _View) {}
}

namespace NRound168 {
// Releasing what the program has through a local reference to it.
unsigned g_nUsers;
struct CLease {
  CLease() { ++g_nUsers; }
  CLease(CLease const &) { ++g_nUsers; }
  ~CLease() {
    auto &nUsers = g_nUsers;
    --nUsers;
  }
};
void fg_Visit(CLease _Lease) {}
// CHECK-MESSAGES: :[[@LINE-1]]:22: warning: parameter '_Lease' takes 'CLease' by value; take it by 'const &' when the function borrows it or by '&&' when it consumes it [malterlib-parameter-ownership]
// A pointer kept divided by its alignment.
using umint = __UINTPTR_TYPE__;
struct COwner {
  umint m_PointerDivAlign;
  explicit COwner(int *_pData)
      : m_PointerDivAlign(reinterpret_cast<umint>(_pData) / alignof(int)) {}
  COwner(COwner const &) = delete;
  ~COwner() {
    delete reinterpret_cast<int *>(m_PointerDivAlign * alignof(int));
  }
};
void fg_Consume(COwner _Owner) {}
// CHECK-MESSAGES: :[[@LINE-1]]:24: warning: parameter '_Owner' takes 'COwner' by value; take it by 'const &' when the function borrows it or by '&&' when it consumes it [malterlib-parameter-ownership]
}

namespace NRound169 {
// Released through a local aggregate made with it.
struct CReleaseArgs {
  int *m_pData;
};
struct COwner {
  int *m_pData;
  COwner(COwner const &) = delete;
  ~COwner() {
    CReleaseArgs Args{m_pData};
    delete Args.m_pData;
  }
};
void fg_Borrow(COwner _Owner) {}
// CHECK-MESSAGES: :[[@LINE-1]]:23: warning: parameter '_Owner' takes 'COwner' by value; take it by 'const &' when the function borrows it or by '&&' when it consumes it [malterlib-parameter-ownership]
// Not writing a local copy through a pointer to it.
struct CView {
  int *m_pData;
  ~CView() {
    int *pCopy = m_pData;
    int **ppCopy = &pCopy;
    *ppCopy = nullptr;
  }
};
void fg_BorrowView(CView _View) {}
}

namespace NRound170 {
// Assigned through a local reference to the local.
struct COwner {
  int *m_pData;
  explicit COwner(int *_pData) : m_pData(_pData) {}
  COwner(COwner const &) = delete;
  ~COwner() {
    int *pData = nullptr;
    int *&pAlias = pData;
    pAlias = m_pData;
    delete pData;
  }
};
void fg_Borrow(COwner _Value) {}
// CHECK-MESSAGES: :[[@LINE-1]]:23: warning: parameter '_Value' takes 'COwner' by value; take it by 'const &' when the function borrows it or by '&&' when it consumes it [malterlib-parameter-ownership]
struct CView {
  int *m_pData;
  ~CView() {
    int *pData = m_pData;
    int *&pAlias = pData;
    pAlias = new int(*pData);
    delete pData;
  }
};
void fg_Visit(CView _Value) {}
}

namespace NRound171 {
// Not a guard of a local copy of the data.
struct CReset {
  int &m_Value;
  ~CReset() { m_Value = 0; }
};
struct CView {
  int *m_pData;
  ~CView() {
    int Value = *m_pData;
    CReset Reset{Value};
  }
};
void fg_Read(CView _View) {}
}

namespace NRound172 {
// Not given through a local pointer to a function that only reads it.
void fg_Observe(int const *_pValue) {}
struct CView {
  int const *m_pValue;
  ~CView() {
    auto fObserve = &fg_Observe;
    fObserve(m_pValue);
  }
};
void fg_Run(CView _View) {}
}

namespace NRound173 {
// Deleted through a pointer to a local aggregate, and what its member is
// assigned after it is made.
struct COwner {
  int *m_pData;
  COwner(COwner const &_Source) : m_pData(new int(*_Source.m_pData)) {}
  ~COwner() {
    struct CArgs {
      int *m_pData;
    };
    CArgs Args{m_pData};
    CArgs *pArgs = &Args;
    delete pArgs->m_pData;
  }
};
void fg_Borrow(COwner _Owner) {}
// CHECK-MESSAGES: :[[@LINE-1]]:23: warning: parameter '_Owner' takes 'COwner' by value; take it by 'const &' when the function borrows it or by '&&' when it consumes it [malterlib-parameter-ownership]
struct CView {
  int const *m_pData;
  ~CView() {
    struct CArgs {
      int const *m_pData;
    };
    CArgs Args{m_pData};
    Args.m_pData = new int(*m_pData);
    delete Args.m_pData;
  }
};
void fg_BorrowView(CView _View) {}
struct CAssigned {
  int *m_pData;
  CAssigned(CAssigned const &) = delete;
  ~CAssigned() {
    struct CArgs {
      int *m_pData;
    };
    CArgs Args{};
    Args.m_pData = m_pData;
    delete Args.m_pData;
  }
};
void fg_BorrowAssigned(CAssigned _Owner) {}
// CHECK-MESSAGES: :[[@LINE-1]]:34: warning: parameter '_Owner' takes 'CAssigned' by value; take it by 'const &' when the function borrows it or by '&&' when it consumes it [malterlib-parameter-ownership]
}

namespace NRound174 {
// Released through a virtual function whose override can release.
struct CResource {
  virtual void f_Release() {}
};
struct CHeapResource : CResource {
  int *m_pData = new int;
  void f_Release() override { delete m_pData; }
};
struct COwner {
  CResource *m_pResource;
  explicit COwner(CResource *_pResource) : m_pResource(_pResource) {}
  COwner(COwner const &) = delete;
  ~COwner() { m_pResource->f_Release(); }
};
void fg_Consume(COwner _Owner) {}
// CHECK-MESSAGES: :[[@LINE-1]]:24: warning: parameter '_Owner' takes 'COwner' by value; take it by 'const &' when the function borrows it or by '&&' when it consumes it [malterlib-parameter-ownership]
// Not through one that is final.
struct CFinalResource final {
  virtual void f_Release() {}
};
struct CFinalOwner {
  CFinalResource *m_pResource;
  explicit CFinalOwner(CFinalResource *_pResource) : m_pResource(_pResource) {}
  CFinalOwner(CFinalOwner const &) = delete;
  ~CFinalOwner() { m_pResource->f_Release(); }
};
void fg_ConsumeFinal(CFinalOwner _Owner) {}
}

namespace NRound176 {
// Deleted through a local aggregate that a pointer to it is assigned.
struct CArgs {
  int *m_pData;
};
struct COwner {
  int *m_pData;
  explicit COwner(int *_pData) : m_pData(_pData) {}
  COwner(COwner const &) = delete;
  ~COwner() {
    CArgs Args{};
    CArgs *pArgs = &Args;
    pArgs->m_pData = m_pData;
    delete Args.m_pData;
  }
};
void fg_Borrow(COwner _Owner) {}
// CHECK-MESSAGES: :[[@LINE-1]]:23: warning: parameter '_Owner' takes 'COwner' by value; take it by 'const &' when the function borrows it or by '&&' when it consumes it [malterlib-parameter-ownership]
}

namespace NRound177 {
void fg_Clear(unsigned &_Value) { _Value = 0; }
struct CState {
  unsigned m_Value;
};
// Clearing a member of a member, which is its own.
struct CView {
  CState m_State;
  ~CView() { fg_Clear(m_State.m_Value); }
};
void fg_View(CView _View) {}
// Clearing a member of a scratch copy.
struct CBorrowing {
  CState const *m_pState;
  ~CBorrowing() {
    CState State = *m_pState;
    fg_Clear(State.m_Value);
  }
};
void fg_Borrowing(CBorrowing _View) {}
}

namespace NRound178 {
// Clearing an array of its own through a range loop.
struct CCache {
  int m_Data[1];
  ~CCache() {
    for (int &Value : m_Data)
      Value = 0;
  }
};
void fg_Cache(CCache _Cache) {}
// Clearing its own member through a cast pointer.
struct CWiped {
  int m_Value;
  ~CWiped() { *reinterpret_cast<unsigned char *>(&m_Value) = 0; }
};
void fg_Wiped(CWiped _Wiped) {}
}

namespace NRound179 {
// A const member function called through a pointer to it.
struct CView {
  void f_Observe() const {}
  ~CView() { (this->*&CView::f_Observe)(); }
};
void fg_Run(CView _View) {}
}

namespace NRound180 {
// Updating shared state through a reference to a record.
struct CState {
  unsigned m_nUsers;
};
struct COwner {
  CState &m_State;
  COwner(COwner const &_Other) : m_State(_Other.m_State) { ++m_State.m_nUsers; }
  ~COwner() { --m_State.m_nUsers; }
};
void fg_Borrow(COwner _Owner) {}
// CHECK-MESSAGES: :[[@LINE-1]]:23: warning: parameter '_Owner' takes 'COwner' by value; take it by 'const &' when the function borrows it or by '&&' when it consumes it [malterlib-parameter-ownership]
}

namespace NRound181 {
// A helper with a lambda it never calls and what isn't evaluated.
void fg_Reset(int **_ppData) {
  auto fUnused = [_ppData] { delete *_ppData; };
  (void)fUnused;
  (void)sizeof(**_ppData = 0);
  *_ppData = nullptr;
}
struct CView {
  int *m_pData;
  ~CView() { fg_Reset(&m_pData); }
};
void fg_Borrow(CView _View) {}
}

namespace NRound182 {
struct CState {
  int *m_pData;
};
// A helper that returns what it is given only in a discarded branch.
int *fg_Data(CState const &_State) {
  if constexpr (false)
    return _State.m_pData;
  return new int(*_State.m_pData);
}
struct CScratchView {
  CState m_State;
  ~CScratchView() { delete fg_Data(m_State); }
};
void fg_Scratch(CScratchView _View) {}
// An aggregate guard that keeps what it is given in a member its destructor
// doesn't use.
struct CGuard {
  int *m_pObserved;
  int *m_pScratch;
  ~CGuard() { delete m_pScratch; }
};
struct CGuarded {
  int *m_pData;
  ~CGuarded() { CGuard Scratch{m_pData, new int(*m_pData)}; }
};
void fg_Guarded(CGuarded _View) {}
}

namespace NRound183 {
// A temporary guard that keeps what it is given in a member its destructor
// doesn't use.
struct CGuard {
  int *m_pObserved;
  int *m_pScratch;
  ~CGuard() { delete m_pScratch; }
};
void fg_Observe(int *_pData) { (void)CGuard{_pData, new int(1)}; }
struct CView {
  int *m_pData;
  ~CView() { fg_Observe(m_pData); }
};
void fg_Work(CView _View) {}
}

namespace NRound184 {
// What owns through a helper that masks tag bits away from a pointer it is
// given, like the links of an AVL tree.
using umint = unsigned long;
struct CLink {
  CLink *m_pNext;
  static CLink *fs_GetPtr(CLink const *_pLink) {
    return (CLink *)((umint)_pLink & (~umint(0x1)));
  }
};
struct CTree {
  CLink *m_pRoot = nullptr;
  ~CTree() { delete CLink::fs_GetPtr(m_pRoot); }
};
void fg_Read(CTree _Tree) {}
// CHECK-MESSAGES: :[[@LINE-1]]:20: warning: parameter '_Tree' takes 'CTree' by value; take it by 'const &' when the function borrows it or by '&&' when it consumes it [malterlib-parameter-ownership]
}

namespace NRound185 {
// An optional destroys what it holds through a visitor given a reference into
// its storage, which a getter that isn't const gives, in a lambda called
// where it is made, which captures the visitor: one of a scalar releases
// nothing, one of an owner does.
template <typename tf_CType>
void fg_CallDestructor(tf_CType &_Value) {
  _Value.~tf_CType();
}
struct CVisitorDestruct {
  template <typename tf_CType>
  void operator()(tf_CType &_Value) {
    fg_CallDestructor(_Value);
  }
};
template <typename t_CType>
struct TCOptional {
  alignas(t_CType) unsigned char mp_Storage[sizeof(t_CType)];
  bool mp_bValid = false;
  t_CType &fp_GetAsStorage() {
    t_CType *pRet = (t_CType *)mp_Storage;
    return *pRet;
  }
  template <typename t_CThis, typename t_CVisitor>
  static void fs_Call(t_CThis &&_pThis, t_CVisitor &&_Visitor) {
    _Visitor(_pThis->fp_GetAsStorage());
  }
  template <typename t_CVisitor>
  void fp_Visit(t_CVisitor &&_Visitor) {
    [&] { fs_Call(this, static_cast<t_CVisitor &&>(_Visitor)); }();
  }
  ~TCOptional() {
    if (mp_bValid)
      fp_Visit(CVisitorDestruct());
  }
};
struct CString {
  char *m_pData = nullptr;
  ~CString() { delete[] m_pData; }
};
void fg_Process(TCOptional<int> _Value) {}
void fg_ProcessString(TCOptional<CString> _Value) {}
// CHECK-MESSAGES: :[[@LINE-1]]:43: warning: parameter '_Value' takes 'TCOptional<CString>' by value; take it by 'const &' when the function borrows it or by '&&' when it consumes it [malterlib-parameter-ownership]
void fg_Call() {
  fg_Process({});
  fg_ProcessString({});
}
}

namespace NRound188 {
// A view whose destructor takes another reference to what it borrows through
// a getter that copies it, and releases only that.
struct CResource {
  unsigned m_nUsers = 1;
};
template <typename t_CType>
struct TCSharedPointer {
  t_CType *m_pPointTo;
  TCSharedPointer(TCSharedPointer const &_Other) : m_pPointTo(_Other.m_pPointTo) {
    ++m_pPointTo->m_nUsers;
  }
  ~TCSharedPointer() {
    if (--m_pPointTo->m_nUsers == 0)
      delete m_pPointTo;
  }
};
struct CInfo {
  TCSharedPointer<CResource> m_pResource;
  TCSharedPointer<CResource> f_Resource() const { return m_pResource; }
};
struct CView {
  CInfo const *m_pInfo;
  ~CView() { auto pResource = m_pInfo->f_Resource(); }
};
void fg_Observe(CView _View) {}
}

namespace NRound190 {
// Released by a helper given an aggregate made with what the object owns.
struct CCapturedDelete {
  void *m_pMemory = nullptr;
  unsigned long m_Size = 0;
};
void fg_Free(CCapturedDelete _Delete) {
  delete[] static_cast<unsigned char *>(_Delete.m_pMemory);
}
void fg_Log(CCapturedDelete _Delete) {}
struct CBuffer {
  unsigned char *m_pData;
  explicit CBuffer(unsigned char *_pData) : m_pData(_pData) {}
  CBuffer(CBuffer const &) = delete;
  ~CBuffer() { fg_Free({m_pData, 16}); }
};
struct CLogged {
  unsigned char *m_pData;
  ~CLogged() { fg_Log({m_pData, 16}); }
};
void fg_Process(CBuffer _Buffer) {}
// CHECK-MESSAGES: :[[@LINE-1]]:25: warning: parameter '_Buffer' takes 'CBuffer' by value; take it by 'const &' when the function borrows it or by '&&' when it consumes it [malterlib-parameter-ownership]
void fg_Observe(CLogged _Logged) {}
}

namespace NRound191 {
// A view whose destructor locks a weak pointer it borrows, taking a reference
// of its own that it releases.
struct CCount {
  unsigned m_nStrong = 1;
  bool f_IncreaseWhileValid() {
    if (!m_nStrong)
      return false;
    ++m_nStrong;
    return true;
  }
};
template <typename t_CType>
struct TCSharedPointer {
  CCount *m_pCount = nullptr;
  TCSharedPointer() = default;
  TCSharedPointer(TCSharedPointer const &) = delete;
  TCSharedPointer(TCSharedPointer &&_Other) : m_pCount(_Other.m_pCount) {
    _Other.m_pCount = nullptr;
  }
  void fp_SetCounted(CCount *_pCount) { m_pCount = _pCount; }
  ~TCSharedPointer() {
    if (m_pCount && --m_pCount->m_nStrong == 0)
      delete m_pCount;
  }
  explicit operator bool() const { return m_pCount; }
};
template <typename t_CType>
struct TCWeakPointer {
  CCount *m_pCount;
  TCSharedPointer<t_CType> f_Lock() const {
    TCSharedPointer<t_CType> pReturn;
    if (!m_pCount || !m_pCount->f_IncreaseWhileValid())
      return pReturn;
    pReturn.fp_SetCounted(m_pCount);
    return pReturn;
  }
};
void fg_Log(bool _bValue);
struct CWeakView {
  TCWeakPointer<int> const *m_pWeak;
  ~CWeakView() {
    auto pKeepAlive = m_pWeak->f_Lock();
    fg_Log(bool(pKeepAlive));
  }
};
void fg_Observe(CWeakView _View) {}
}

namespace NRound192 {
// Releasing what a member points to through an exchange, which moves it out.
template <typename t_CType>
t_CType &&fg_Move(t_CType &_Value) { return static_cast<t_CType &&>(_Value); }
template <typename t_CType>
t_CType &&fg_Forward(t_CType &_Value) { return static_cast<t_CType &&>(_Value); }
template <typename tf_CDestination, typename t_CSetTo>
auto fg_Exchange(tf_CDestination &_Destination, t_CSetTo &&_SetTo) {
  auto Temp = fg_Move(_Destination);
  _Destination = fg_Forward<t_CSetTo>(_SetTo);
  return Temp;
}
struct COwner {
  int *m_pData;
  explicit COwner(int *_pData) : m_pData(_pData) {}
  COwner(COwner const &) = delete;
  ~COwner() { delete fg_Exchange(m_pData, nullptr); }
};
void fg_ByValue(COwner _Owner) {}
// CHECK-MESSAGES: :[[@LINE-1]]:24: warning: parameter '_Owner' takes 'COwner' by value; take it by 'const &' when the function borrows it or by '&&' when it consumes it [malterlib-parameter-ownership]
void fg_Use() { fg_ByValue(COwner(new int(1))); }
}

namespace NRound201 {
// Destroying an object of a class that is trivially destroyed, like a wide
// integer, releases nothing; one of an owner releases what it owns.
template <typename tf_CType>
void fg_CallDestructor(tf_CType &_Value) {
  _Value.~tf_CType();
}
template <typename t_CType>
struct TCOptional {
  alignas(t_CType) unsigned char mp_Storage[sizeof(t_CType)];
  bool mp_bValid = false;
  ~TCOptional() {
    if (mp_bValid)
      fg_CallDestructor(*(t_CType *)mp_Storage);
  }
};
struct CUInt256 {
  unsigned long m_Words[4];
};
struct CString {
  char *m_pData = nullptr;
  ~CString() { delete[] m_pData; }
};
void fg_Process(TCOptional<CUInt256> _Bits) {}
void fg_ProcessString(TCOptional<CString> _Value) {}
// CHECK-MESSAGES: :[[@LINE-1]]:43: warning: parameter '_Value' takes 'TCOptional<CString>' by value; take it by 'const &' when the function borrows it or by '&&' when it consumes it [malterlib-parameter-ownership]
// Also destroyed in its destructor directly.
template <typename t_CType>
struct TCInPlace {
  alignas(t_CType) unsigned char mp_Storage[sizeof(t_CType)];
  ~TCInPlace() { ((t_CType *)mp_Storage)->~t_CType(); }
};
void fg_ProcessInPlace(TCInPlace<CUInt256> _Bits) {}
void fg_ProcessStringInPlace(TCInPlace<CString> _Value) {}
// CHECK-MESSAGES: :[[@LINE-1]]:49: warning: parameter '_Value' takes 'TCInPlace<CString>' by value; take it by 'const &' when the function borrows it or by '&&' when it consumes it [malterlib-parameter-ownership]
void fg_Call() {
  fg_Process({});
  fg_ProcessString({});
  fg_ProcessInPlace({});
  fg_ProcessStringInPlace({});
}
}

namespace NRound202 {
// Destroying an object through a virtual destructor by a helper runs the
// destructor of what it is, which can release.
template <typename tf_CType>
void fg_CallDestructor(tf_CType &_Value) {
  _Value.~tf_CType();
}
struct CVirtualDestroyBase {
  virtual ~CVirtualDestroyBase() = default;
};
struct CBuffer : CVirtualDestroyBase {
  int *m_pData;
  ~CBuffer() { delete m_pData; }
};
struct CInPlaceOwner {
  CVirtualDestroyBase *m_pObject;
  CInPlaceOwner(CInPlaceOwner const &) = delete;
  ~CInPlaceOwner() { fg_CallDestructor(*m_pObject); }
};
struct CPlain {
  int m_Value;
};
struct CInPlacePlain {
  CPlain *m_pObject;
  ~CInPlacePlain() { fg_CallDestructor(*m_pObject); }
};
void fg_Process(CInPlaceOwner _Owner) {}
// CHECK-MESSAGES: :[[@LINE-1]]:31: warning: parameter '_Owner' takes 'CInPlaceOwner' by value; take it by 'const &' when the function borrows it or by '&&' when it consumes it [malterlib-parameter-ownership]
void fg_ProcessPlain(CInPlacePlain _Plain) {}
}

namespace NRound209 {
// Released through a const member function of what a member points to that
// writes what is mutable, like `TCIntrusiveRefCount::f_Decrease()`.
struct CAtomic {
  int m_Value;
  int f_FetchSub(int _Value) { return __atomic_fetch_sub(&m_Value, _Value, 3); }
  int f_FetchAdd(int _Value) { return __atomic_fetch_add(&m_Value, _Value, 3); }
};
struct CCount {
  mutable CAtomic m_RefCount;
  int f_Decrease() const { return m_RefCount.f_FetchSub(1); }
  int f_Increase() const { return m_RefCount.f_FetchAdd(1); }
  int f_Get() const { return 0; }
};
struct CCountedReference {
  CCount const *m_pCount;
  explicit CCountedReference(CCount const &_Count) : m_pCount(&_Count) {}
  CCountedReference(CCountedReference const &_Other)
      : m_pCount(_Other.m_pCount) {
    m_pCount->f_Increase();
  }
  ~CCountedReference() { m_pCount->f_Decrease(); }
};
void fg_Borrow(CCountedReference _Reference) {}
// CHECK-MESSAGES: :[[@LINE-1]]:34: warning: parameter '_Reference' takes 'CCountedReference' by value; take it by 'const &' when the function borrows it or by '&&' when it consumes it [malterlib-parameter-ownership]
// Not through one that only reads it.
struct CReadingReference {
  CCount const *m_pCount;
  CReadingReference(CReadingReference const &_Other);
  ~CReadingReference() { m_pCount->f_Get(); }
};
void fg_Read(CReadingReference _Reference) {}

// Released by a helper through a virtual member function that an override
// can make release.
struct ICResource {
  virtual void f_Release() {}
};
struct CResource : ICResource {
  int *m_pData;
  void f_Release() override { delete m_pData; }
};
void fg_Release(ICResource *_pResource) { _pResource->f_Release(); }
struct CResourceHandle {
  ICResource *m_pResource;
  explicit CResourceHandle(ICResource *_pResource) : m_pResource(_pResource) {}
  CResourceHandle(CResourceHandle const &) = delete;
  ~CResourceHandle() { fg_Release(m_pResource); }
};
void fg_Consume(CResourceHandle _Resource) {}
// CHECK-MESSAGES: :[[@LINE-1]]:33: warning: parameter '_Resource' takes 'CResourceHandle' by value; take it by 'const &' when the function borrows it or by '&&' when it consumes it [malterlib-parameter-ownership]
}

namespace NRound210 {
// Released through a const member function that calls another of its class
// that writes what is mutable.
struct CAtomic {
  int m_Value;
  int f_FetchSub(int _Value) { return __atomic_fetch_sub(&m_Value, _Value, 3); }
  int f_FetchAdd(int _Value) { return __atomic_fetch_add(&m_Value, _Value, 3); }
};
struct CCount {
  mutable CAtomic m_RefCount;
  int fp_Decrease() const { return m_RefCount.f_FetchSub(1); }
  int f_Decrease() const { return fp_Decrease(); }
  int f_Increase() const { return m_RefCount.f_FetchAdd(1); }
};
struct CCountedReference {
  CCount const *m_pCount;
  CCountedReference(CCountedReference const &_Other)
      : m_pCount(_Other.m_pCount) {
    m_pCount->f_Increase();
  }
  ~CCountedReference() { m_pCount->f_Decrease(); }
};
void fg_Borrow(CCountedReference _Reference) {}
// CHECK-MESSAGES: :[[@LINE-1]]:34: warning: parameter '_Reference' takes 'CCountedReference' by value; take it by 'const &' when the function borrows it or by '&&' when it consumes it [malterlib-parameter-ownership]
}

namespace NRound211 {
// Not a scratch copy made through a helper of the constructor that only reads
// what it is given to allocate its own.
struct CStr {
  char *m_pData;
  CStr(char const *_pText) { f_SetStr(_pText); }
  void f_SetStr(char const *_pText) { m_pData = new char[2]{*_pText, 0}; }
  ~CStr() { delete[] m_pData; }
};
void fg_Log(CStr const &_Text);
struct CTextView {
  char const *m_pText;
  ~CTextView() {
    CStr Text(m_pText);
    fg_Log(Text);
  }
};
void fg_Read(CTextView _Text) {}
// But one whose helper keeps it.
struct CKept {
  char const *m_pText;
  CKept(char const *_pText) { f_Set(_pText); }
  void f_Set(char const *_pText) { m_pText = _pText; }
  ~CKept() { delete[] m_pText; }
};
struct CKeptView {
  char const *m_pText;
  ~CKeptView() { CKept Kept(m_pText); }
};
void fg_ReadKept(CKeptView _Text) {}
// CHECK-MESSAGES: :[[@LINE-1]]:28: warning: parameter '_Text' takes 'CKeptView' by value; take it by 'const &' when the function borrows it or by '&&' when it consumes it [malterlib-parameter-ownership]
}

namespace NRound213 {
// Released through a count in what a member points to.
struct CAtomic {
  int m_Value;
  int f_FetchSub(int _Value) { return __atomic_fetch_sub(&m_Value, _Value, 3); }
  int f_FetchAdd(int _Value) { return __atomic_fetch_add(&m_Value, _Value, 3); }
};
struct CIntrusiveRefCount {
  mutable CAtomic m_RefCount;
  int f_Decrease() const { return m_RefCount.f_FetchSub(1); }
  int f_Increase() const { return m_RefCount.f_FetchAdd(1); }
};
struct CResource {
  CIntrusiveRefCount m_RefCount;
};
struct CResourceLease {
  CResource const *m_pResource;
  CResourceLease(CResourceLease const &_Other) : m_pResource(_Other.m_pResource) {
    m_pResource->m_RefCount.f_Increase();
  }
  ~CResourceLease() { m_pResource->m_RefCount.f_Decrease(); }
};
void fg_Borrow(CResourceLease _Lease) {}
// CHECK-MESSAGES: :[[@LINE-1]]:31: warning: parameter '_Lease' takes 'CResourceLease' by value; take it by 'const &' when the function borrows it or by '&&' when it consumes it [malterlib-parameter-ownership]
}

namespace NRound214 {
// Released through an element an overloaded subscript of what a member points
// to gives.
struct CUsers {
  unsigned long *m_pData;
  unsigned long &operator[](unsigned long _Index) { return m_pData[_Index]; }
};
struct CUsage {
  CUsers *m_pUsers;
  unsigned long m_Index;
  CUsage(CUsers &_Users, unsigned long _Index) : m_pUsers(&_Users), m_Index(_Index) {
    ++_Users[_Index];
  }
  CUsage(CUsage const &) = delete;
  ~CUsage() { --(*m_pUsers)[m_Index]; }
};
void fg_Work(CUsage _Usage) {}
// CHECK-MESSAGES: :[[@LINE-1]]:21: warning: parameter '_Usage' takes 'CUsage' by value; take it by 'const &' when the function borrows it or by '&&' when it consumes it [malterlib-parameter-ownership]
}

namespace NRound226 {
// A count released through an atomic of what a member points to, which
// changes its value through an `atomic_ref`, like `TCAtomic::f_FetchSub`,
// also of what is cast to not const, but not one that loads it.
template <typename t_CType>
struct TCAtomicIndeterminate {
  t_CType m_Value;
  template <typename tf_CType>
  t_CType f_FetchSub(tf_CType _Value) noexcept {
    return std::atomic_ref<t_CType>(m_Value).fetch_sub(_Value);
  }
  template <typename tf_CType>
  t_CType f_FetchSubShared(tf_CType _Value) const noexcept {
    return std::atomic_ref<t_CType>(const_cast<t_CType &>(m_Value))
        .fetch_sub(_Value);
  }
  template <typename tf_CType>
  t_CType f_FetchAdd(tf_CType _Value) noexcept {
    return __atomic_fetch_add(&m_Value, _Value, 3);
  }
  t_CType f_Load() noexcept { return std::atomic_ref<t_CType>(m_Value).load(); }
};
template <typename t_CType>
struct TCAtomic : public TCAtomicIndeterminate<t_CType> {};
struct CLease {
  TCAtomic<unsigned> *m_pCount;
  CLease(TCAtomic<unsigned> &_Count) : m_pCount(&_Count) {
    m_pCount->f_FetchAdd(1u);
  }
  CLease(CLease const &_Other) : m_pCount(_Other.m_pCount) {
    m_pCount->f_FetchAdd(1u);
  }
  ~CLease() { m_pCount->f_FetchSub(1u); }
};
void fg_Work(CLease _Lease) {}
// CHECK-MESSAGES: :[[@LINE-1]]:21: warning: parameter '_Lease' takes 'CLease' by value; take it by 'const &' when the function borrows it or by '&&' when it consumes it [malterlib-parameter-ownership]
struct CSharedLease {
  TCAtomic<unsigned> const *m_pCount;
  CSharedLease(CSharedLease const &_Other);
  ~CSharedLease() { m_pCount->f_FetchSubShared(1u); }
};
void fg_WorkShared(CSharedLease _Lease) {}
// CHECK-MESSAGES: :[[@LINE-1]]:33: warning: parameter '_Lease' takes 'CSharedLease' by value; take it by 'const &' when the function borrows it or by '&&' when it consumes it [malterlib-parameter-ownership]
struct CObserver {
  TCAtomic<unsigned> *m_pCount;
  CObserver(CObserver const &_Other);
  ~CObserver() { m_pCount->f_Load(); }
};
void fg_Observe(CObserver _Observer) {}
}

namespace NRound233 {
// A count released atomically by a helper, and an owner a const member
// function moves out of what it holds.
void fg_ReleaseCount(unsigned *_pCount) {
  __atomic_fetch_sub(_pCount, 1u, __ATOMIC_RELEASE);
}
struct CLease {
  unsigned *m_pCount;
  CLease(CLease const &);
  ~CLease() { fg_ReleaseCount(m_pCount); }
};
void fg_Work(CLease _Lease) {}
// CHECK-MESSAGES: :[[@LINE-1]]:21: warning: parameter '_Lease' takes 'CLease' by value; take it by 'const &' when the function borrows it or by '&&' when it consumes it [malterlib-parameter-ownership]
template <typename t_CType>
struct TCUniquePointer {
  t_CType *m_pValue = nullptr;
  TCUniquePointer() = default;
  TCUniquePointer(TCUniquePointer &&_Other) : m_pValue(_Other.m_pValue) {
    _Other.m_pValue = nullptr;
  }
  ~TCUniquePointer() { delete m_pValue; }
};
struct CSlot {
  mutable TCUniquePointer<int> m_pValue;
  TCUniquePointer<int> f_Take() const {
    return static_cast<TCUniquePointer<int> &&>(m_pValue);
  }
};
struct CSlotCleanup {
  CSlot const *m_pSlot;
  explicit CSlotCleanup(CSlot const &_Slot) : m_pSlot(&_Slot) {}
  CSlotCleanup(CSlotCleanup const &) = delete;
  ~CSlotCleanup() { m_pSlot->f_Take(); }
};
void fg_WorkSlot(CSlotCleanup _Cleanup) {}
// CHECK-MESSAGES: :[[@LINE-1]]:31: warning: parameter '_Cleanup' takes 'CSlotCleanup' by value; take it by 'const &' when the function borrows it or by '&&' when it consumes it [malterlib-parameter-ownership]
}

namespace NRound234 {
// Released through a local reference to what a member points to, and
// through it dereferenced.
struct CAtomic {
  int m_Value;
  int f_FetchSub(int _Value) { return __atomic_fetch_sub(&m_Value, _Value, 3); }
};
struct CCount {
  mutable CAtomic m_RefCount;
  int f_Decrease() const { return m_RefCount.f_FetchSub(1); }
  int f_Get() const { return 0; }
};
struct CCountLease {
  CCount const *m_pCount;
  CCountLease(CCountLease const &) = delete;
  CCountLease(CCountLease &&);
  ~CCountLease() {
    auto &Count = *m_pCount;
    Count.f_Decrease();
  }
};
void fg_Borrow(CCountLease _Lease) {}
// CHECK-MESSAGES: :[[@LINE-1]]:28: warning: parameter '_Lease' takes 'CCountLease' by value; take it by 'const &' when the function borrows it or by '&&' when it consumes it [malterlib-parameter-ownership]
struct CDereferenced {
  CCount const *m_pCount;
  CDereferenced(CDereferenced const &) = delete;
  CDereferenced(CDereferenced &&);
  ~CDereferenced() { (*m_pCount).f_Decrease(); }
};
void fg_Dereferenced(CDereferenced _Lease) {}
// CHECK-MESSAGES: :[[@LINE-1]]:36: warning: parameter '_Lease' takes 'CDereferenced' by value; take it by 'const &' when the function borrows it or by '&&' when it consumes it [malterlib-parameter-ownership]
// Not through one that only reads it.
struct CReading {
  CCount const *m_pCount;
  CReading(CReading const &) = delete;
  CReading(CReading &&);
  ~CReading() {
    auto &Count = *m_pCount;
    Count.f_Get();
  }
};
void fg_Read(CReading _Lease) {}
}

namespace NRound237 {
int fg_Close(int _Descriptor);
// A handle copied out of the object, which is the same handle.
struct CPipe {
  int m_Descriptors[2];
  CPipe(int _Read, int _Write) : m_Descriptors{_Read, _Write} {}
  CPipe(CPipe const &) = delete;
  CPipe(CPipe &&);
  ~CPipe() {
    for (int Descriptor : m_Descriptors)
      fg_Close(Descriptor);
  }
};
void fg_Process(CPipe _Pipe) {}
// CHECK-MESSAGES: :[[@LINE-1]]:23: warning: parameter '_Pipe' takes 'CPipe' by value; take it by 'const &' when the function borrows it or by '&&' when it consumes it [malterlib-parameter-ownership]
struct CState {
  int m_Descriptor;
};
struct CFile {
  CState *m_pState;
  CFile(CFile const &) = delete;
  CFile(CFile &&);
  ~CFile() {
    int Descriptor = m_pState->m_Descriptor;
    fg_Close(Descriptor);
  }
};
void fg_Read(CFile _File) {}
// CHECK-MESSAGES: :[[@LINE-1]]:20: warning: parameter '_File' takes 'CFile' by value; take it by 'const &' when the function borrows it or by '&&' when it consumes it [malterlib-parameter-ownership]
}

namespace NRound238 {
void fg_Lock();
void fg_Unlock();
template <typename t_FCleanup>
struct TCOnScopeExit {
  t_FCleanup m_fCleanup;
  TCOnScopeExit(t_FCleanup &&_fCleanup)
      : m_fCleanup(static_cast<t_FCleanup &&>(_fCleanup)) {}
  TCOnScopeExit(TCOnScopeExit const &) = delete;
  ~TCOnScopeExit() { m_fCleanup(); }
};
struct COnScopeExitHelper {
  template <typename t_FCleanup>
  auto operator/(t_FCleanup &&_fCleanup) const {
    return TCOnScopeExit<t_FCleanup>(static_cast<t_FCleanup &&>(_fCleanup));
  }
};
extern COnScopeExitHelper const &g_OnScopeExit;
// Released by a scope exit of its destructor.
struct CLock {
  CLock() { fg_Lock(); }
  CLock(CLock const &) = delete;
  CLock(CLock &&);
  ~CLock() {
    auto Cleanup = g_OnScopeExit / [] { fg_Unlock(); };
  }
};
void fg_Work(CLock _Lock) {}
// CHECK-MESSAGES: :[[@LINE-1]]:20: warning: parameter '_Lock' takes 'CLock' by value; take it by 'const &' when the function borrows it or by '&&' when it consumes it [malterlib-parameter-ownership]
}

namespace NRound242 {
struct CIntrusiveRefCount {
  mutable int m_RefCount;
  int f_Increase() const {
    return __atomic_fetch_add(&m_RefCount, 1, __ATOMIC_RELAXED);
  }
  int f_Decrease() const {
    return __atomic_fetch_sub(&m_RefCount, 1, __ATOMIC_RELEASE);
  }
};
void fg_Decrease(CIntrusiveRefCount const *_pCount) { _pCount->f_Decrease(); }
// Released through a helper that calls a const member function that writes
// what is mutable.
struct CCountedReference {
  CIntrusiveRefCount const *m_pCount;
  CCountedReference(CCountedReference const &_Other) : m_pCount(_Other.m_pCount) {
    m_pCount->f_Increase();
  }
  ~CCountedReference() { fg_Decrease(m_pCount); }
};
void fg_Borrow(CCountedReference _Reference) {}
// CHECK-MESSAGES: :[[@LINE-1]]:34: warning: parameter '_Reference' takes 'CCountedReference' by value; take it by 'const &' when the function borrows it or by '&&' when it consumes it [malterlib-parameter-ownership]
}
namespace NRound243 {
struct CRefCount {
  mutable unsigned m_nUsers;
};
void fg_ReleaseReference(CRefCount const *_pCount) { --_pCount->m_nUsers; }
// Released through a helper that writes what is mutable through a pointer to
// const.
struct CReferenceHold {
  CRefCount const *m_pCount;
  CReferenceHold(CReferenceHold const &) = delete;
  ~CReferenceHold() { fg_ReleaseReference(m_pCount); }
};
void fg_Borrow(CReferenceHold _Hold) {}
// CHECK-MESSAGES: :[[@LINE-1]]:31: warning: parameter '_Hold' takes 'CReferenceHold' by value; take it by 'const &' when the function borrows it or by '&&' when it consumes it [malterlib-parameter-ownership]
}
namespace NRound244 {
template <typename t_CType>
t_CType &&fg_Move(t_CType &_Value) { return static_cast<t_CType &&>(_Value); }
template <typename t_CType>
struct TCUniquePointer {
  t_CType *m_pValue = nullptr;
  TCUniquePointer() = default;
  TCUniquePointer(TCUniquePointer &&_Other) : m_pValue(_Other.m_pValue) {
    _Other.m_pValue = nullptr;
  }
  ~TCUniquePointer() { delete m_pValue; }
};
struct CSlot {
  TCUniquePointer<int> m_pValue;
};
// Released through a const member function that moves an owner out of what a
// member points to.
struct CCleanup {
  explicit CCleanup(CSlot &_Slot) : m_pSlot(&_Slot) {}
  CCleanup(CCleanup const &) = delete;
  CSlot *m_pSlot;
  TCUniquePointer<int> f_Take() const { return fg_Move(m_pSlot->m_pValue); }
  ~CCleanup() { auto pReleased = f_Take(); }
};
void fg_Work(CCleanup _Cleanup) {}
// CHECK-MESSAGES: :[[@LINE-1]]:23: warning: parameter '_Cleanup' takes 'CCleanup' by value; take it by 'const &' when the function borrows it or by '&&' when it consumes it [malterlib-parameter-ownership]
void fg_Use(CSlot &_Slot) { fg_Work(CCleanup(_Slot)); }
}

namespace NRound328 {
// What owns what a tagged pointer points to, which it deletes through the
// getter that decodes the pointer from its integer member.
using umint = unsigned long;
template <typename t_CType, int t_nBits = 1>
struct TCBitStorePointer {
  TCBitStorePointer(t_CType *_pPtr = nullptr) : mp_PointTo(fsp_ToInt(_pPtr)) {}
  TCBitStorePointer(TCBitStorePointer const &_Other) : mp_PointTo(_Other.mp_PointTo) {}
  TCBitStorePointer &operator=(TCBitStorePointer const &_Other) {
    mp_PointTo = _Other.mp_PointTo;
    return *this;
  }
  t_CType *f_Get() const { return fsp_FromInt(mp_PointTo); }
  t_CType *operator->() const { return fsp_FromInt(mp_PointTo); }
  static umint fsp_ToInt(t_CType *_pPtr) { return umint(_pPtr) >> t_nBits; }
  static constexpr t_CType *fsp_FromInt(umint _Pointer) {
    return (t_CType *)(_Pointer << t_nBits);
  }
  umint mp_PointTo : sizeof(umint) * 8 - t_nBits;
  umint mp_Bits : t_nBits;
};
struct CTaggedOwner {
  TCBitStorePointer<int> m_pData = nullptr;
  CTaggedOwner() = default;
  CTaggedOwner(CTaggedOwner const &) = delete;
  ~CTaggedOwner() { delete m_pData.f_Get(); }
};
void fg_Process(CTaggedOwner _Value) {}
// CHECK-MESSAGES: :[[@LINE-1]]:30: warning: parameter '_Value' takes 'CTaggedOwner' by value; take it by 'const &' when the function borrow
void fg_Use() { fg_Process(CTaggedOwner{}); }
}

namespace NRound330 {
// A count of users that the destructor decrements by an operator of what a
// pointer member points to, which the copies share.
using uint32 = unsigned;
template <typename t_CType> struct TCAtomic {
  t_CType m_Value;
  t_CType operator++() { return __atomic_add_fetch(&m_Value, 1, __ATOMIC_SEQ_CST); }
  t_CType operator--() { return __atomic_sub_fetch(&m_Value, 1, __ATOMIC_SEQ_CST); }
  t_CType f_FetchSub(t_CType _Value) { return __atomic_fetch_sub(&m_Value, _Value, __ATOMIC_SEQ_CST); }
};
struct CLease {
  TCAtomic<uint32> *m_pCount;
  explicit CLease(TCAtomic<uint32> &_Count) : m_pCount(&_Count) { ++*m_pCount; }
  CLease(CLease const &_Other) : m_pCount(_Other.m_pCount) { ++*m_pCount; }
  ~CLease() { --*m_pCount; }
};
void fg_Read(CLease _Lease) {}
// CHECK-MESSAGES: :[[@LINE-1]]:21: warning: parameter '_Lease' takes 'CLease' by value; take it by 'const &' when the function borrows it or by '&&' when it consumes it [malterlib-parameter-ownership]
}

namespace NRound331 {
// A const helper that gives back an owner it makes of what it exchanges out
// of a mutable member gives what the object held, and one that deletes what
// the object owns releases it, whatever it gives back.
template <typename t_CType>
t_CType &&fg_Move(t_CType &_Value) { return static_cast<t_CType &&>(_Value); }
template <typename t_CType>
t_CType &&fg_Forward(t_CType &_Value) { return static_cast<t_CType &&>(_Value); }
template <typename tf_CDestination, typename t_CSetTo>
auto fg_Exchange(tf_CDestination &_Destination, t_CSetTo &&_SetTo) {
  auto Temp = fg_Move(_Destination);
  _Destination = fg_Forward<t_CSetTo>(_SetTo);
  return Temp;
}
template <typename t_CType> struct TCUniquePointer {
  t_CType *m_pValue;
  explicit TCUniquePointer(t_CType *_pValue) : m_pValue(_pValue) {}
  TCUniquePointer(TCUniquePointer const &) = delete;
  ~TCUniquePointer() { delete m_pValue; }
};
struct CSlot {
  mutable int *m_pValue = nullptr;
  TCUniquePointer<int> f_Take() const {
    return TCUniquePointer<int>(fg_Exchange(m_pValue, nullptr));
  }
};
struct CReleaseSlot {
  CSlot const *m_pSlot;
  explicit CReleaseSlot(CSlot const *_pSlot) : m_pSlot(_pSlot) {}
  CReleaseSlot(CReleaseSlot const &) = delete;
  ~CReleaseSlot() { m_pSlot->f_Take(); }
};
void fg_Use(CReleaseSlot _Guard) {}
// CHECK-MESSAGES: :[[@LINE-1]]:26: warning: parameter '_Guard' takes 'CReleaseSlot' by value; take it by 'const &' when the function borrows it or by '&&' when it consumes it [malterlib-parameter-ownership]
template <typename t_CType> struct TCOptional {
  t_CType m_Value{};
  bool m_bValid = false;
  ~TCOptional() {}
};
struct CResource {
  CResource() = default;
  CResource(CResource const &) = delete;
  unsigned long *m_pData = nullptr;
  ~CResource() { fp_Release(); }
private:
  TCOptional<unsigned long> fp_Release() const {
    delete m_pData;
    return {};
  }
};
void fg_Consume(CResource _Resource) {}
// CHECK-MESSAGES: :[[@LINE-1]]:27: warning: parameter '_Resource' takes 'CResource' by value; take it by 'const &' when the function borrows it or by '&&' when it consumes it [malterlib-parameter-ownership]
void fg_Instantiate() { fg_Consume(CResource{}); }
}

namespace NRound333 {
// A count written through a local reference to what a member points to, also
// a member of it, and released through a const virtual function, which an
// override can implement by releasing.
using umint = unsigned long;
struct CEntry {
  umint m_RefCount = 1;
};
struct CReference {
  CEntry *mp_pEntry;
  explicit CReference(CEntry &_Entry) : mp_pEntry(&_Entry) { ++_Entry.m_RefCount; }
  CReference(CReference const &_Other) : mp_pEntry(_Other.mp_pEntry) { ++mp_pEntry->m_RefCount; }
  ~CReference() {
    auto &Entry = *mp_pEntry;
    --Entry.m_RefCount;
  }
};
void fg_UseReference(CReference _Reference) {}
// CHECK-MESSAGES: :[[@LINE-1]]:33: warning: parameter '_Reference' takes 'CReference' by value; take it by 'const &' when the function borrows it or by '&&' when it consumes it [malterlib-parameter-ownership]
struct ICResource {
  virtual void f_Release() const {}
};
struct CHandle {
  ICResource const *m_pResource;
  CHandle(CHandle const &);
  ~CHandle() { m_pResource->f_Release(); }
};
void fg_UseHandle(CHandle _Handle) {}
// CHECK-MESSAGES: :[[@LINE-1]]:27: warning: parameter '_Handle' takes 'CHandle' by value; take it by 'const &' when the function borrows it or by '&&' when it consumes it [malterlib-parameter-ownership]
}

namespace NRound334 {
// A const virtual one called through a helper, whose override can release
// what is mutable, like when called directly.
struct ICResource {
  virtual void f_Release() const {}
};
struct CResource : ICResource {
  mutable unsigned m_nUsers = 1;
  void f_Release() const override { --m_nUsers; }
};
void fg_Release(ICResource const *_pResource) { _pResource->f_Release(); }
struct CHandle {
  ICResource const *m_pResource;
  CHandle(CHandle const &_Other);
  ~CHandle() { fg_Release(m_pResource); }
};
void fg_Process(CHandle _Handle) {}
// CHECK-MESSAGES: :[[@LINE-1]]:25: warning: parameter '_Handle' takes 'CHandle' by value; take it by 'const &' when the function borrows it or by '&&' when it consumes it [malterlib-parameter-ownership]
// But a helper that locks a weak pointer it borrows takes a reference it
// releases, like when the lock is called directly.
struct CCounted {
  unsigned m_nStrong = 1;
};
template <typename t_CType> struct TCSharedPointer {
  t_CType *m_pPointTo = nullptr;
  TCSharedPointer() = default;
  TCSharedPointer(TCSharedPointer const &_Other) : m_pPointTo(_Other.m_pPointTo) {
    if (m_pPointTo)
      ++m_pPointTo->m_nStrong;
  }
  ~TCSharedPointer() {
    if (m_pPointTo && --m_pPointTo->m_nStrong == 0)
      delete m_pPointTo;
  }
};
template <typename t_CType> struct TCWeakPointer {
  t_CType *m_pPointTo;
  TCSharedPointer<t_CType> f_Lock() const {
    TCSharedPointer<t_CType> pReturn;
    if (m_pPointTo->m_nStrong) {
      ++m_pPointTo->m_nStrong;
      pReturn.m_pPointTo = m_pPointTo;
    }
    return pReturn;
  }
};
auto fg_Lock(TCWeakPointer<CCounted> const &_pWeak) -> TCSharedPointer<CCounted> {
  return _pWeak.f_Lock();
}
struct CView {
  TCWeakPointer<CCounted> const *m_pWeak;
  ~CView() { auto pStrong = fg_Lock(*m_pWeak); }
};
void fg_Observe(CView _View) {}
}
namespace NRound334Local {
// A count written through a local reference to it, also through a member
// function called on that reference.
struct CRefCount {
  mutable unsigned m_nUsers = 1;
  void f_Decrease() const {
    auto &nUsers = m_nUsers;
    --nUsers;
  }
};
struct CSubscription {
  CRefCount *m_pCount;
  explicit CSubscription(CRefCount *_pCount) : m_pCount(_pCount) {}
  CSubscription(CSubscription const &) = delete;
  ~CSubscription() { m_pCount->f_Decrease(); }
};
void fg_Process(CSubscription _Subscription) {}
// CHECK-MESSAGES: :[[@LINE-1]]:31: warning: parameter '_Subscription' takes 'CSubscription' by value; take it by 'const &' when the function borrows it or by '&&' when it consumes it [malterlib-parameter-ownership]
template <typename t_CType> struct TCAtomic {
  t_CType m_Value;
  t_CType f_FetchSub(t_CType _Value) {
    return __atomic_fetch_sub(&m_Value, _Value, __ATOMIC_RELEASE);
  }
  t_CType f_FetchAdd(t_CType _Value) {
    return __atomic_fetch_add(&m_Value, _Value, __ATOMIC_RELAXED);
  }
};
template <typename t_CType> struct TCIntrusiveRefCount {
  mutable TCAtomic<t_CType> m_RefCount;
  void f_Increase() const { m_RefCount.f_FetchAdd(1); }
  void f_Decrease() const {
    auto &RefCount = m_RefCount;
    RefCount.f_FetchSub(1);
  }
};
struct CReferenceLease {
  TCIntrusiveRefCount<long> *m_pCount;
  CReferenceLease(CReferenceLease const &_Other) : m_pCount(_Other.m_pCount) {
    m_pCount->f_Increase();
  }
  ~CReferenceLease() { m_pCount->f_Decrease(); }
};
void fg_Read(CReferenceLease _Lease) {}
// CHECK-MESSAGES: :[[@LINE-1]]:30: warning: parameter '_Lease' takes 'CReferenceLease' by value; take it by 'const &' when the function borrows it or by '&&' when it consumes it [malterlib-parameter-ownership]
}

namespace NRound336Copy
{
	// A copy of what is const, also through a helper that counts it, takes a
	// reference of its own, which it releases itself.
	struct CRefCount
	{
		int m_Count = 1;

		void f_Increase()
		{
			++m_Count;
		}

		int f_Decrease()
		{
			return --m_Count;
		}
	};

	template <typename t_CType>
	struct TCData
	{
		CRefCount m_RefCount;
		t_CType m_Value;
	};

	template <typename t_CType>
	class TCSharedPointer
	{
		TCData<t_CType> *m_pPointTo = nullptr;

		void fp_SetInit(TCData<t_CType> *_pPointTo)
		{
			if (_pPointTo)
				_pPointTo->m_RefCount.f_Increase();
			m_pPointTo = _pPointTo;
		}

	public:
		TCSharedPointer(TCSharedPointer const &_Other)
		{
			fp_SetInit(_Other.m_pPointTo);
		}

		~TCSharedPointer()
		{
			if (m_pPointTo && m_pPointTo->m_RefCount.f_Decrease() == 0)
				delete m_pPointTo;
		}
	};

	struct CBorrowed
	{
		TCSharedPointer<int> const *m_pPointer;

		~CBorrowed()
		{
			TCSharedPointer<int> pSnapshot(*m_pPointer);
		}
	};

	void fg_Inspect(CBorrowed _View)
	{
	}
}

namespace NRound337Copy {
// A copy constructor that makes its copy in a base it constructs makes one of
// its own, which it destroys itself.
struct CStrImp {
  char *m_pData;
  CStrImp(CStrImp const &_Other);
  ~CStrImp() { delete[] m_pData; }
};
CStrImp::CStrImp(CStrImp const &_Other) : m_pData(new char[1]) {
  m_pData[0] = _Other.m_pData[0];
}
struct CStr : CStrImp {
  CStr(CStr const &_Str) : CStrImp(_Str) {}
};
void fg_Log(CStr const &_Text);
struct CLogView {
  CStr const *m_pText;
  ~CLogView() {
    CStr Message(*m_pText);
    fg_Log(Message);
  }
};
void fg_Observe(CLogView _View) {}
}

namespace NRound337Owned {
// What a const member function caches through its object with the const cast
// away, `const_cast<CStrPtr *>(this)->m_Len`, is its own, and a copy made of
// what the object holds by value whose constructor writes what is mutable of
// it releases nothing, unlike one of what a member points to.
struct CStrPtr {
  char const *m_pData;
  unsigned long m_Len;
  unsigned long f_GetLen() const {
    const_cast<CStrPtr *>(this)->m_Len = 0;
    return m_Len;
  }
};
struct CStr {
  CStr(CStrPtr const &_From) { m_pData = new char[_From.f_GetLen() + 1]; }
  CStr(CStr const &_Other);
  ~CStr() { delete[] m_pData; }
  char *m_pData;
};
struct CTextView {
  CStrPtr m_Text;
  ~CTextView() { CStr Scratch(m_Text); }
};
void fg_ObserveText(CTextView _View) {}
struct CState {
  mutable int m_nRefs;
  void f_Decrease() const { --m_nRefs; }
};
struct CDrop {
  CDrop(CState const &_State) { _State.f_Decrease(); }
};
struct CHolder {
  CState m_State;
  int *m_pOther;
  ~CHolder() { CDrop Drop(m_State); }
};
void fg_ObserveHolder(CHolder _Holder) {}
struct CPointing {
  CState *m_pState;
  ~CPointing() { CDrop Drop(*m_pState); }
};
void fg_ObservePointing(CPointing _Pointing) {}
// CHECK-MESSAGES: :[[@LINE-1]]:35: warning: parameter '_Pointing' takes 'CPointing' by value; take it by 'const &' when the function borrows it or by '&&' when it consumes it [malterlib-parameter-ownership]
struct CCaching {
  int *m_pData;
  mutable int m_Len;
  void f_Set() const { m_Len = 1; }
  ~CCaching() { const_cast<CCaching *>(this)->f_Set(); }
};
void fg_ObserveCaching(CCaching _Caching) {}
}


namespace NRound337Extern {
// A number given to a member of an instantiation another file makes, whose
// instantiation there tells what it does, releases nothing, but a pointer
// given to it can.
template <typename t_CTraits> struct TCStr {
  TCStr() = default;
  template <typename tf_CTraitsF> TCStr(TCStr<tf_CTraitsF> const &_From) {
    f_SetStr(_From.f_GetStr(), _From.f_GetLen());
  }
  char const *f_GetStr() const { return m_pData; }
  long f_GetLen() const { return m_Len; }
  void f_SetStr(char const *_pStr, long _Len);
  char const *m_pData = nullptr;
  long m_Len = 0;
};
template <typename t_CTraits>
void TCStr<t_CTraits>::f_SetStr(char const *_pStr, long _Len) {
  m_pData = _pStr;
  m_Len = _Len;
}
struct CTraitsDynamic;
struct CTraitsPtr;
extern template struct TCStr<CTraitsDynamic>;
using CStr = TCStr<CTraitsDynamic>;
using CStrPtr = TCStr<CTraitsPtr>;
struct CTextView {
  CStrPtr m_Text;
  ~CTextView() { CStr Scratch(m_Text); }
};
void fg_ObserveText(CTextView _View) {}
struct CState {
  int m_nRefs;
};
struct CRef {
  CState *m_pState;
};
template <typename t_CTag> struct TCHandle {
  TCHandle(CRef const &_Ref) { f_Share(_Ref.m_pState); }
  void f_Share(CState *_pState);
  CState *m_pState = nullptr;
};
template <typename t_CTag> void TCHandle<t_CTag>::f_Share(CState *_pState) {
  m_pState = _pState;
  ++m_pState->m_nRefs;
}
extern template struct TCHandle<int>;
struct COwner {
  CRef m_Ref;
  ~COwner() { TCHandle<int> Handle(m_Ref); }
};
void fg_ObserveOwner(COwner _Owner) {}
// CHECK-MESSAGES: :[[@LINE-1]]:29: warning: parameter '_Owner' takes 'COwner' by value; take it by 'const &' when the function borrows it or by '&&' when it consumes it [malterlib-parameter-ownership]
}


namespace NRound338Getter {
using umint = unsigned long;
// What a getter of a member gives a reference to through what that points to,
// `--m_Count.f_Get()` of a reference wrapper, is shared with copies.
template <typename t_CType> struct TCReference {
  t_CType *m_pObject;
  TCReference(t_CType &_Object) : m_pObject(&_Object) {}
  t_CType &f_Get() const { return *m_pObject; }
};
struct CCountGuard {
  TCReference<umint> m_Count;

  CCountGuard(umint &_Count) : m_Count(_Count) { ++_Count; }
  CCountGuard(CCountGuard const &_Other) : m_Count(_Other.m_Count) {
    ++m_Count.f_Get();
  }
  ~CCountGuard() { --m_Count.f_Get(); }
};
void fg_Use(CCountGuard _Guard) {}
// CHECK-MESSAGES: :[[@LINE-1]]:25: warning: parameter '_Guard' takes 'CCountGuard' by value; take it by 'const &' when the function borrows it or by '&&' when it consumes it [malterlib-parameter-ownership]
// Not what a getter gives a reference to in the member itself.
template <typename t_CType> struct TCBox {
  t_CType m_Value;
  t_CType &f_Get() { return m_Value; }
};
struct CLocalCount {
  TCBox<umint> m_Count;
  ~CLocalCount() { --m_Count.f_Get(); }
};
void fg_UseLocal(CLocalCount _Count) {}
}

namespace NRound339 {
using umint = unsigned long;
// What an overloaded `operator*` of a member gives through what that points
// to is shared with copies.
template <typename t_CType> struct TCPointer {
  t_CType *m_pPointTo;
  TCPointer(t_CType *_pPointTo) : m_pPointTo(_pPointTo) {}
  t_CType &operator*() const { return *m_pPointTo; }
};
struct CCountGuard {
  TCPointer<umint> m_pCount;
  explicit CCountGuard(umint &_Count) : m_pCount(&_Count) { ++*m_pCount; }
  CCountGuard(CCountGuard const &_Other) : m_pCount(_Other.m_pCount) {
    ++*m_pCount;
  }
  ~CCountGuard() { --*m_pCount; }
};
void fg_Borrow(CCountGuard _Guard) {}
// CHECK-MESSAGES: :[[@LINE-1]]:28: warning: parameter '_Guard' takes 'CCountGuard' by value; take it by 'const &' when the function borrows it or by '&&' when it consumes it [malterlib-parameter-ownership]
// Not what it gives of the member itself.
template <typename t_CType> struct TCBox {
  t_CType m_Value;
  t_CType &operator*() { return m_Value; }
};
struct CLocalCount {
  TCBox<umint> m_Count;
  ~CLocalCount() { --*m_Count; }
};
void fg_UseLocal(CLocalCount _Count) {}
// An overloaded decrement of what a pointer in a member points to, also
// read into a local, changes what copies share.
template <typename t_CType> struct TCAtomic {
  t_CType m_Value;
  t_CType operator++() {
    return __atomic_add_fetch(&m_Value, 1, __ATOMIC_SEQ_CST);
  }
  t_CType operator--() {
    return __atomic_sub_fetch(&m_Value, 1, __ATOMIC_SEQ_CST);
  }
};
struct CState {
  TCAtomic<unsigned> m_nUsers;
};
struct CNestedGuard {
  struct CData {
    CState *m_pState;
  } m_Data;
  CNestedGuard(CState &_State) : m_Data{&_State} {
    ++m_Data.m_pState->m_nUsers;
  }
  CNestedGuard(CNestedGuard const &_Other) : m_Data(_Other.m_Data) {
    ++m_Data.m_pState->m_nUsers;
  }
  ~CNestedGuard() { --m_Data.m_pState->m_nUsers; }
};
void fg_UseNested(CNestedGuard _Guard) {}
// CHECK-MESSAGES: :[[@LINE-1]]:32: warning: parameter '_Guard' takes 'CNestedGuard' by value; take it by 'const &' when the function borrows it or by '&&' when it consumes it [malterlib-parameter-ownership]
struct CLocalGuard {
  CState *m_pState;
  CLocalGuard(CState &_State) : m_pState(&_State) { ++m_pState->m_nUsers; }
  CLocalGuard(CLocalGuard const &_Other) : m_pState(_Other.m_pState) {
    ++m_pState->m_nUsers;
  }
  ~CLocalGuard() {
    CState *pState = m_pState;
    --pState->m_nUsers;
  }
};
void fg_UseLocalGuard(CLocalGuard _Guard) {}
// CHECK-MESSAGES: :[[@LINE-1]]:35: warning: parameter '_Guard' takes 'CLocalGuard' by value; take it by 'const &' when the function borrows it or by '&&' when it consumes it [malterlib-parameter-ownership]
// Not of its own member through a local pointer to it.
struct COwnCount {
  CState m_State;
  ~COwnCount() {
    CState *pState = &m_State;
    --pState->m_nUsers;
  }
};
void fg_UseOwn(COwnCount _Count) {}
// A helper reached on many paths, also one that calls itself, is looked into
// once.
template <int t_Depth> void fg_Inspect(int const *_p) {
  if (*_p)
    fg_Inspect<t_Depth - 1>(_p);
  else
    fg_Inspect<t_Depth - 1>(_p + 1);
}
template <> void fg_Inspect<0>(int const *_p) {
  if (*_p)
    fg_Inspect<0>(_p + 1);
}
struct CObserver {
  int const *m_pData;
  ~CObserver() { fg_Inspect<40>(m_pData); }
};
void fg_Observe(CObserver _Observer) { (void)_Observer; }
}

namespace NRound340Nested {
// Also a member of a member of what a pointer of it points to.
struct CState
{
  struct CReferences { unsigned m_nUsers; };
  CReferences m_References;
};

struct CSubscription
{
  CState *m_pState;

  CSubscription(CSubscription const &_Other)
    : m_pState(_Other.m_pState)
  {
    ++m_pState->m_References.m_nUsers;
  }

  ~CSubscription()
  {
    --m_pState->m_References.m_nUsers;
  }
};

void fg_Observe(CSubscription _Subscription) {}
// CHECK-MESSAGES: :[[@LINE-1]]:31: warning: parameter '_Subscription' takes 'CSubscription' by value; take it by 'const &' when the function borrows it or by '&&' when it consumes it [malterlib-parameter-ownership]
}

namespace NRound341InitCapture {
// An init capture of a lambda a guard keeps is a member of the lambda, which
// the guard releases where it is destroyed.
template <typename t_FCleanup> struct TCOnScopeExit {
  t_FCleanup m_fCleanup;
  TCOnScopeExit(t_FCleanup &&_fCleanup)
      : m_fCleanup(static_cast<t_FCleanup &&>(_fCleanup)) {}
  TCOnScopeExit(TCOnScopeExit const &) = delete;
  TCOnScopeExit(TCOnScopeExit &&_Other);
  ~TCOnScopeExit() { m_fCleanup(); }
};
struct COnScopeExitHelper {
  template <typename t_FCleanup>
  auto operator/(t_FCleanup &&_fCleanup) const {
    return TCOnScopeExit<t_FCleanup>(static_cast<t_FCleanup &&>(_fCleanup));
  }
};
extern COnScopeExitHelper const &g_OnScopeExit;
template <typename t_CType> t_CType &&fg_Move(t_CType &_Value) {
  return static_cast<t_CType &&>(_Value);
}
template <typename tf_CCleanup> void fg_Consume(tf_CCleanup _Cleanup) {}
// CHECK-MESSAGES: :[[@LINE-1]]:61: warning: parameter '_Cleanup' takes 'NRound341InitCapture::TCOnScopeExit<(lambda at {{.*}})>' by value; take it by 'const &' when the function borrows it or by '&&' when it consumes it [malterlib-parameter-ownership]
void fg_Test(int *_pData) {
  auto Cleanup = g_OnScopeExit / [pData = _pData] { delete pData; };
  fg_Consume(fg_Move(Cleanup));
}
}

namespace NRound355HelperCycle {
// Helpers of a cycle that each call the one before twice are each looked
// into once while the cycle is, not once for each path to them.
void fg_Run0(int *, unsigned);
void fg_Run1(int *p, unsigned depth) { fg_Run0(p, depth); fg_Run0(p, depth); }
void fg_Run2(int *p, unsigned depth) { fg_Run1(p, depth); fg_Run1(p, depth); }
void fg_Run3(int *p, unsigned depth) { fg_Run2(p, depth); fg_Run2(p, depth); }
void fg_Run4(int *p, unsigned depth) { fg_Run3(p, depth); fg_Run3(p, depth); }
void fg_Run5(int *p, unsigned depth) { fg_Run4(p, depth); fg_Run4(p, depth); }
void fg_Run6(int *p, unsigned depth) { fg_Run5(p, depth); fg_Run5(p, depth); }
void fg_Run7(int *p, unsigned depth) { fg_Run6(p, depth); fg_Run6(p, depth); }
void fg_Run8(int *p, unsigned depth) { fg_Run7(p, depth); fg_Run7(p, depth); }
void fg_Run9(int *p, unsigned depth) { fg_Run8(p, depth); fg_Run8(p, depth); }
void fg_Run10(int *p, unsigned depth) { fg_Run9(p, depth); fg_Run9(p, depth); }
void fg_Run11(int *p, unsigned depth) { fg_Run10(p, depth); fg_Run10(p, depth); }
void fg_Run12(int *p, unsigned depth) { fg_Run11(p, depth); fg_Run11(p, depth); }
void fg_Run13(int *p, unsigned depth) { fg_Run12(p, depth); fg_Run12(p, depth); }
void fg_Run14(int *p, unsigned depth) { fg_Run13(p, depth); fg_Run13(p, depth); }
void fg_Run15(int *p, unsigned depth) { fg_Run14(p, depth); fg_Run14(p, depth); }
void fg_Run16(int *p, unsigned depth) { fg_Run15(p, depth); fg_Run15(p, depth); }
void fg_Run17(int *p, unsigned depth) { fg_Run16(p, depth); fg_Run16(p, depth); }
void fg_Run18(int *p, unsigned depth) { fg_Run17(p, depth); fg_Run17(p, depth); }
void fg_Run19(int *p, unsigned depth) { fg_Run18(p, depth); fg_Run18(p, depth); }
void fg_Run20(int *p, unsigned depth) { fg_Run19(p, depth); fg_Run19(p, depth); }
void fg_Run21(int *p, unsigned depth) { fg_Run20(p, depth); fg_Run20(p, depth); }
void fg_Run22(int *p, unsigned depth) { fg_Run21(p, depth); fg_Run21(p, depth); }
void fg_Run23(int *p, unsigned depth) { fg_Run22(p, depth); fg_Run22(p, depth); }
void fg_Run24(int *p, unsigned depth) { fg_Run23(p, depth); fg_Run23(p, depth); }
void fg_Run25(int *p, unsigned depth) { fg_Run24(p, depth); fg_Run24(p, depth); }
void fg_Run26(int *p, unsigned depth) { fg_Run25(p, depth); fg_Run25(p, depth); }
void fg_Run27(int *p, unsigned depth) { fg_Run26(p, depth); fg_Run26(p, depth); }
void fg_Run28(int *p, unsigned depth) { fg_Run27(p, depth); fg_Run27(p, depth); }
void fg_Run29(int *p, unsigned depth) { fg_Run28(p, depth); fg_Run28(p, depth); }
void fg_Run30(int *p, unsigned depth) { fg_Run29(p, depth); fg_Run29(p, depth); }
void fg_Run31(int *p, unsigned depth) { fg_Run30(p, depth); fg_Run30(p, depth); }
void fg_Run32(int *p, unsigned depth) { fg_Run31(p, depth); fg_Run31(p, depth); }
void fg_Run33(int *p, unsigned depth) { fg_Run32(p, depth); fg_Run32(p, depth); }
void fg_Run34(int *p, unsigned depth) { fg_Run33(p, depth); fg_Run33(p, depth); }
void fg_Run35(int *p, unsigned depth) { fg_Run34(p, depth); fg_Run34(p, depth); }
void fg_Run36(int *p, unsigned depth) { fg_Run35(p, depth); fg_Run35(p, depth); }
void fg_Run37(int *p, unsigned depth) { fg_Run36(p, depth); fg_Run36(p, depth); }
void fg_Run38(int *p, unsigned depth) { fg_Run37(p, depth); fg_Run37(p, depth); }
void fg_Run39(int *p, unsigned depth) { fg_Run38(p, depth); fg_Run38(p, depth); }
void fg_Run40(int *p, unsigned depth) { fg_Run39(p, depth); fg_Run39(p, depth); }
void fg_Run0(int *p, unsigned depth) { if (depth) fg_Run40(p, depth - 1); }
struct CView { int *m_pValue; ~CView() { fg_Run40(m_pValue, 0); } };
void fg_Test(CView _View) {}
}
