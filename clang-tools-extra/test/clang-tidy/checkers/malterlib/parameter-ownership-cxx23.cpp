// RUN: %check_clang_tidy -std=c++23-or-later %s malterlib-parameter-ownership %t -- -config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}"

// An operator with an explicit object is given the object as its parameter.
struct COwner
{
	int *m_pData;

	bool operator!(this COwner const &_Self)
	{
		delete _Self.m_pData;
		return true;
	}

	~COwner()
	{
		!*this;
	}
};

void fg_Run(COwner _Owner) {}
// CHECK-MESSAGES: :[[@LINE-1]]:20: warning: parameter '_Owner' takes 'COwner' by value; take it by 'const &' when the function borrows it or by '&&' when it consumes it [malterlib-parameter-ownership]

// Round 209: a member function with an explicit object writes the object
// through it: what it holds by value is its own storage, also through a
// local pointer to a member of it; what a pointer member points to is shared.
struct CExplicitView
{
	~CExplicitView()
	{
		f_Clear();
	}
	void f_Clear(this CExplicitView &_Self)
	{
		auto **ppData = &_Self.m_pData;
		*ppData = nullptr;
	}
	int *m_pData;
};
void fg_UseExplicitView(CExplicitView _Value)
{
}
struct CExplicitShared
{
	~CExplicitShared()
	{
		f_Clear();
	}
	void f_Clear(this CExplicitShared &_Self)
	{
		auto **ppData = &_Self.m_pOther->m_pData;
		*ppData = nullptr;
	}
	CExplicitShared *m_pOther;
	int *m_pData;
};
void fg_UseExplicitShared(CExplicitShared _Value)
// CHECK-MESSAGES: :[[@LINE-1]]:43: warning: parameter '_Value' takes 'CExplicitShared' by value; {{.*}}
{
}
