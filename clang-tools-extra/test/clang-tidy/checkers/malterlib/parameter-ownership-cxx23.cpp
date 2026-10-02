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
