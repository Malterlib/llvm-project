// RUN: %check_clang_tidy -std=c++20-or-later %s malterlib-function-naming %t -- -config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}"

void fg_Global();
// A recursive function has no prefix of its own.
void fgr_Recursive();
// CHECK-MESSAGES: :[[@LINE-1]]:6: warning: global function 'fgr_Recursive' should have the 'fg_' prefix [malterlib-function-naming]
void fg_rRecursive();
// CHECK-MESSAGES: :[[@LINE-1]]:6: warning: global function 'fg_rRecursive' should be upper camel case after its prefix [malterlib-function-naming]
static void fsg_Static();
static void fsgr_StaticRecursive();
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: static global function 'fsgr_StaticRecursive' should have the 'fg_' or 'fsg_' prefix [malterlib-function-naming]

void Global();
// CHECK-MESSAGES: :[[@LINE-1]]:6: warning: global function 'Global' should have the 'fg_' prefix [malterlib-function-naming]
// One declared `static` can also have the prefix of any other.
static void fg_StaticNamedGlobal();
static void StaticUnprefixed();
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: static global function 'StaticUnprefixed' should have the 'fg_' or 'fsg_' prefix [malterlib-function-naming]
// The prefix of another declaration tells that either can be wrong.
void fsg_NotStatic();
// CHECK-MESSAGES: :[[@LINE-1]]:6: warning: global function 'fsg_NotStatic' should have the 'fg_' prefix, or be declared static [malterlib-function-naming]
void fg_lowerCase();
// CHECK-MESSAGES: :[[@LINE-1]]:6: warning: global function 'fg_lowerCase' should be upper camel case after its prefix [malterlib-function-naming]
void fg_lower();
// CHECK-MESSAGES: :[[@LINE-1]]:6: warning: global function 'fg_lower' should be upper camel case after its prefix [malterlib-function-naming]

namespace
{
	void fg_Anonymous();
}

struct CStruct
{
	void f_Member();
	void fr_Recursive();
// CHECK-MESSAGES: :[[@LINE-1]]:7: warning: member function 'fr_Recursive' should have the 'f_' prefix [malterlib-function-naming]
	static void fs_Static();
	static void fsr_StaticRecursive();
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: static member function 'fsr_StaticRecursive' should have the 'fs_' prefix [malterlib-function-naming]
	void Member();
// CHECK-MESSAGES: :[[@LINE-1]]:7: warning: member function 'Member' should have the 'f_' prefix [malterlib-function-naming]
	static void f_Static();
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: static member function 'f_Static' should have the 'fs_' prefix, or not be declared static [malterlib-function-naming]
	void fsp_Both();
// CHECK-MESSAGES: :[[@LINE-1]]:7: warning: member function 'fsp_Both' should have the 'f_' prefix, or be declared static and be moved into the private or protected section [malterlib-function-naming]
	void fg_Global();
// CHECK-MESSAGES: :[[@LINE-1]]:7: warning: member function 'fg_Global' should have the 'f_' prefix [malterlib-function-naming]
	friend void fg_Friend(CStruct &);

	CStruct();
	~CStruct();
	CStruct &operator = (CStruct const &);
	operator bool () const;

	// Lower case names follow the protocols of the language.
	bool await_ready();
	int *begin();

private:
	void fp_Private();
	void fpr_PrivateRecursive();
// CHECK-MESSAGES: :[[@LINE-1]]:7: warning: private or protected member function 'fpr_PrivateRecursive' should have the 'fp_' prefix [malterlib-function-naming]
	static void fsp_Static();
	static void fspr_StaticRecursive();
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: private or protected static member function 'fspr_StaticRecursive' should have the 'fsp_' prefix [malterlib-function-naming]
	void f_Private();
// CHECK-MESSAGES: :[[@LINE-1]]:7: warning: private or protected member function 'f_Private' should have the 'fp_' prefix, or be moved into the public section [malterlib-function-naming]
	static void fs_Private();
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: private or protected static member function 'fs_Private' should have the 'fsp_' prefix, or be moved into the public section [malterlib-function-naming]

protected:
	void fp_Protected();
};

void CStruct::f_Member()
{
	// The name is checked at the declaration only.
}

struct ICInterface
{
	virtual void f_Function() = 0;
	virtual void fp_Odd();
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: member function 'fp_Odd' should have the 'f_' prefix, or be moved into the private or protected section [malterlib-function-naming]
};

struct CImplementation : ICInterface
{
	// Overrides take the names of the functions they override.
	void f_Function() override;

private:
	void f_Function2();
// CHECK-MESSAGES: :[[@LINE-1]]:7: warning: private or protected member function 'f_Function2' should have the 'fp_' prefix, or be moved into the public section [malterlib-function-naming]
	void fp_Odd() override;
};

template <typename t_CType>
struct TCTemplate
{
	void f_Member();
	void Member();
// CHECK-MESSAGES: :[[@LINE-1]]:7: warning: member function 'Member' should have the 'f_' prefix [malterlib-function-naming]
};

template <typename tf_CType>
void fg_Template();

template <typename tf_CType>
void Template();
// CHECK-MESSAGES: :[[@LINE-1]]:6: warning: global function 'Template' should have the 'fg_' prefix [malterlib-function-naming]

template <>
void Template<int>();

extern "C" void c_function();

int main()
{
	TCTemplate<int> Instance;
	Instance.Member();

	auto fLambda = []
		{
		}
	;
	fLambda();
	return 0;
}

#define DDeclareFunction(d_Name) void d_Name();
DDeclareFunction(fg_FromMacro)
DDeclareFunction(FromMacro)
// CHECK-MESSAGES: :[[@LINE-1]]:18: warning: global function 'FromMacro' should have the 'fg_' prefix [malterlib-function-naming]

// A name in the body of a macro is reported once, in the macro.
#define DDeclareBadFunction void BadInMacro();
// CHECK-MESSAGES: :[[@LINE-1]]:34: warning: global function 'BadInMacro' should have the 'fg_' prefix [malterlib-function-naming]
DDeclareBadFunction
DDeclareBadFunction

void NoLint(); // NOLINT(malterlib-function-naming)

// Member functions keep C++ linkage in an `extern "C"` block.
extern "C"
{
	struct CExternCMembers
	{
		void Bad();
// CHECK-MESSAGES: :[[@LINE-1]]:8: warning: member function 'Bad' should have the 'f_' prefix [malterlib-function-naming]
	};
}

// Groups that underscores separate are upper camel case too.
void fg_Good_bad();
// CHECK-MESSAGES: :[[@LINE-1]]:6: warning: global function 'fg_Good_bad' should be upper camel case after its prefix [malterlib-function-naming]
void fg_Group_Value();

// Only the protocols of the language and the standard library are lower case.
void calculate_total();
// CHECK-MESSAGES: :[[@LINE-1]]:6: warning: global function 'calculate_total' should be upper camel case after its prefix [malterlib-function-naming]
// CHECK-MESSAGES: :[[@LINE-2]]:6: warning: global function 'calculate_total' should have the 'fg_' prefix [malterlib-function-naming]
struct CContainerLike
{
	int *begin();
	int *end();
	void push_back(int _Value);
	void my_helper();
// CHECK-MESSAGES: :[[@LINE-1]]:7: warning: member function 'my_helper' should be upper camel case after its prefix [malterlib-function-naming]
// CHECK-MESSAGES: :[[@LINE-2]]:7: warning: member function 'my_helper' should have the 'f_' prefix [malterlib-function-naming]
};

// The protocol of an allocator.
struct CAllocator
{
	int *allocate(unsigned long _nItems);
	void deallocate(int *_pMemory, unsigned long _nItems);
};

// A name doesn't end with an underscore.
void fg_Run_();
// CHECK-MESSAGES: :[[@LINE-1]]:6: warning: {{.*}}'fg_Run_'{{.*}}

// A name has at least two characters after its prefix.
void fg_X();
// CHECK-MESSAGES: :[[@LINE-1]]:6: warning: global function 'fg_X' should have at least 2 characters after its prefix [malterlib-function-naming]

// The members of a specialization of an external template are named by it.
# 1 "external.hpp" 1 3
namespace fmt
{
	template <typename t_CType>
	struct formatter;
}
# 1 "function-naming.cpp" 2
struct CFormatted
{
};
template <>
struct fmt::formatter<CFormatted>
{
	void parse();
	void format();
};

// A member function that overrides one of a dependent base in an
// instantiation takes its name.
struct CRound82Base
{
	virtual void f_Run();
};
template <typename t_CBase>
struct TCRound82Derived : public t_CBase
{
private:
	void f_Run();
};
TCRound82Derived<CRound82Base> g_Round82Instance;

// Also of a partial specialization whose instantiation overrides it.
template <typename t_CBase>
struct TCRound83Derived;
template <typename t_CBase>
struct TCRound83Derived<t_CBase *> : t_CBase
{
private:
	void f_Run();
};
TCRound83Derived<CRound82Base *> g_Round83Instance;

// An override in an instantiation of a class nested in a template.
struct ICRound97Base
{
	virtual void f_Run();
};

template <typename t_CBase>
struct TCRound97Outer
{
	struct CInner : t_CBase
	{
	private:
		void f_Run();
	};
};

TCRound97Outer<ICRound97Base>::CInner g_Round97Inner;

// An override in a class template in a class template keeps the name of the
// function it overrides.
struct CRound108Base
{
	virtual void f_Run()
	{
	}
};

template <typename t_CBase>
struct TCRound108Outer
{
	template <typename t_CValue>
	struct TCInner : t_CBase
	{
	private:
		void f_Run()
		{
		}
	};
};

TCRound108Outer<CRound108Base>::TCInner<int> g_Round108Object;

// An override in a local class of a function template.
template <typename tf_CBase>
void fg_Round110Local()
{
	struct CLocal : tf_CBase
	{
	private:
		void f_Run()
		{
		}
	};
	CLocal Local;
}

void fg_Round110UseLocal()
{
	fg_Round110Local<CRound108Base>();
}

// A local class of a member template of a class template is checked as each
// instantiation of the member template makes it: it overrides here.
struct CRound112Base
{
	virtual void f_Run()
	{
	}
};
template <typename t_COuter>
struct TCRound112Outer
{
	template <typename tf_CBase>
	void f_Use()
	{
		struct CLocal : tf_CBase
		{
		private:
			void f_Run()
			{
			}
		};
		CLocal Local;
	}
};
void fg_Round112UseOuter()
{
	TCRound112Outer<int> Outer;
	Outer.f_Use<CRound112Base>();
}

// Round 120: the functions of the locking protocols of the standard library.
namespace NRound120Lockable {
struct CMutex {
  void lock() {}
  void unlock() {}
  bool try_lock() { return true; }
  void lock_shared() {}
  void unlock_shared() {}
};
}

// Round 125: the customizations of the ranges are standard protocols.
struct CRound125Iterator
{
	int *m_pValue;
	int &operator*() const;
	friend int &&iter_move(CRound125Iterator const &_iValue);
	friend void iter_swap(CRound125Iterator const &_iLeft, CRound125Iterator const &_iRight);
};

namespace NRound152 {
// Hooks of `std::pointer_traits` and `std::allocator_traits`.
struct CPointer
{
	using element_type = int;
	int *m_pValue;
	static CPointer pointer_to(int &_Value)
	{
		return {&_Value};
	}
};

struct CAllocator
{
	int *allocate_at_least(unsigned long _nElements);
};
}

namespace NRound162 {
// A friend that is a member of another class, named there.
template <typename t_CType>
struct TCProvider {
  void f_Run();
};
template <typename t_CType>
struct TCTarget {
  friend void TCProvider<t_CType>::f_Run();
};
TCTarget<int> g_Target;
}

namespace NRound245
{
	// The names of protocols only where the protocol uses them: a public
	// member function, and the free functions it finds by argument dependent
	// lookup.
	int g_Total = 0;
	void clear()
// CHECK-MESSAGES: :[[@LINE-1]]:7: warning: global function 'clear' should be upper camel case after its prefix [malterlib-function-naming]
// CHECK-MESSAGES: :[[@LINE-2]]:7: warning: global function 'clear' should have the 'fg_' prefix [malterlib-function-naming]
	{
		g_Total = 0;
	}
	struct CCounter
	{
		void clear();
		friend void swap(CCounter &, CCounter &);
	private:
		void count();
// CHECK-MESSAGES: :[[@LINE-1]]:8: warning: private or protected member function 'count' should be upper camel case after its prefix [malterlib-function-naming]
// CHECK-MESSAGES: :[[@LINE-2]]:8: warning: private or protected member function 'count' should have the 'fp_' prefix [malterlib-function-naming]
	};
	struct CRange
	{
	};
	int *begin(CRange &);
	int *end(CRange &);
}

// A name with several problems is reported for each, also after the prefix of
// another kind of function.
void fp_fopen_func();
// CHECK-MESSAGES: :[[@LINE-1]]:6: warning: global function 'fp_fopen_func' should be upper camel case after its prefix [malterlib-function-naming]
// CHECK-MESSAGES: :[[@LINE-2]]:6: warning: global function 'fp_fopen_func' should have the 'fg_' prefix [malterlib-function-naming]

// A static member function annotated to be named as if it weren't, to share a
// name with the non-static functions of the same interface, like the f_Alloc of
// the allocators, also one of the same name in a derived class.
#define DMibNamedAsMemberFunction [[clang::annotate("malterlib-named-as-member-function")]]
struct CAllocatorBase
{
	DMibNamedAsMemberFunction static void *f_Alloc(unsigned _Size);
	DMibNamedAsMemberFunction static void f_Free(void *_pMemory);
	static void f_NotAnnotated();
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: static member function 'f_NotAnnotated' should have the 'fs_' prefix, or not be declared static [malterlib-function-naming]
	// One named with the prefix of a static one isn't told to be declared
	// static, which it is.
	DMibNamedAsMemberFunction static void fs_Annotated();
// CHECK-MESSAGES: :[[@LINE-1]]:40: warning: static member function 'fs_Annotated' should have the 'f_' prefix [malterlib-function-naming]
	// Nor to be moved to another section, which leaves its prefix wrong.
private:
	DMibNamedAsMemberFunction static void fs_PrivateAnnotated();
// CHECK-MESSAGES: :[[@LINE-1]]:40: warning: private or protected static member function 'fs_PrivateAnnotated' should have the 'fp_' prefix [malterlib-function-naming]
};
struct CAllocatorDerived : CAllocatorBase
{
	static void *f_Alloc(unsigned _Size);
	static void f_Free(void *_pMemory);
	static void f_Other();
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: static member function 'f_Other' should have the 'fs_' prefix, or not be declared static [malterlib-function-naming]
};
// Also of a base's member whose annotation the definition of its template
// adds after the base was instantiated.
template <typename t_CType>
struct TCLateAllocator
{
	static void *f_Alloc(unsigned _Size);
};
struct CLateAllocatorDerived : TCLateAllocator<int>
{
	static void *f_Alloc(unsigned _Size);
};
template <typename t_CType>
DMibNamedAsMemberFunction void *TCLateAllocator<t_CType>::f_Alloc(unsigned _Size)
{
	return nullptr;
}
// Also of a member template of such a base, whose annotation the definition of
// the template it is instantiated from adds later.
template <typename t_CType>
struct TCLateTemplateAllocator
{
	template <typename tf_CSize>
	static void *f_Alloc(tf_CSize _Size);
};
struct CLateTemplateAllocatorDerived : TCLateTemplateAllocator<int>
{
	static void *f_Alloc(unsigned _Size);
};
template <typename t_CType>
template <typename tf_CSize>
DMibNamedAsMemberFunction void *TCLateTemplateAllocator<t_CType>::f_Alloc(tf_CSize _Size)
{
	return nullptr;
}
// Also of a base a class template is given, as its instantiations know it.
template <typename t_CBase>
struct TCAllocatorAdapter : t_CBase
{
	static void *f_Alloc(unsigned _Size);
	static void f_Other();
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: static member function 'f_Other' should have the 'fs_' prefix, or not be declared static [malterlib-function-naming]
};
template struct TCAllocatorAdapter<CAllocatorBase>;
// Also of a partial specialization, and of a member class of a class
// template.
template <typename t_CBase>
struct TCPartialAdapter;
template <typename t_CBase>
struct TCPartialAdapter<t_CBase *> : t_CBase
{
	static void *f_Alloc(unsigned _Size);
};
template struct TCPartialAdapter<CAllocatorBase *>;
template <typename t_CBase>
struct TCOuterAdapter
{
	struct CInner : t_CBase
	{
		static void *f_Alloc(unsigned _Size);
	};
};
template struct TCOuterAdapter<CAllocatorBase>::CInner;
// Nor of one only an incomplete specialization of is named, which isn't
// instantiated.
template <typename t_CBase>
struct TCPointedAdapter : t_CBase
{
	static void *f_Alloc(unsigned _Size);
};
TCPointedAdapter<CAllocatorBase> *g_pPointedAdapter;
// One complete instantiation that tells is enough, whatever the incomplete ones.
struct CPlainAllocatorBase
{
};
template <typename t_CBase>
struct TCMixedAdapter : t_CBase
{
	static void *f_Alloc(unsigned _Size);
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: static member function 'f_Alloc' should have the 'fs_' prefix, or not be declared static [malterlib-function-naming]
};
template struct TCMixedAdapter<CPlainAllocatorBase>;
TCMixedAdapter<CAllocatorBase> *g_pMixedAdapter;
// Of one nothing instantiates, which can't be told, either prefix.
template <typename t_CBase>
struct TCUninstantiatedAdapter : t_CBase
{
	static void *f_Alloc(unsigned _Size);
	static void fs_Free(void *_pMemory);
};
// Also a member template.
struct CTemplateAllocatorBase
{
	template <typename t_CType>
	DMibNamedAsMemberFunction static t_CType *f_Alloc();
	template <typename t_CType>
	static t_CType *f_NotAnnotated();
// CHECK-MESSAGES: :[[@LINE-1]]:18: warning: static member function 'f_NotAnnotated' should have the 'fs_' prefix, or not be declared static [malterlib-function-naming]
};
struct CTemplateAllocatorDerived : CTemplateAllocatorBase
{
	template <typename t_CType>
	static t_CType *f_Alloc();
	template <typename t_CType>
	static t_CType *f_NotAnnotated();
// CHECK-MESSAGES: :[[@LINE-1]]:18: warning: static member function 'f_NotAnnotated' should have the 'fs_' prefix, or not be declared static [malterlib-function-naming]
};
