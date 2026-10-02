// RUN: clang-tidy --checks='-*,malterlib-parameter-ownership' --config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" %s -- -std=c++20 2>&1 | FileCheck --allow-empty %s
// RUN: %clang -fplugin-arg-malterlib-options-file=%S/Inputs/ClangTidyOptions.yaml -std=c++20 -fsyntax-only -Wno-uninitialized -fplugin-arg-malterlib-checks='-*,malterlib-parameter-ownership' %s 2>&1 | FileCheck --allow-empty %s
// CHECK-NOT: [malterlib-

// A value that leads back to itself, a member of a local aggregate made with
// that member, also through a function that gives back what it is given,
// isn't told to be of the object: the destructor's walk ends there.

struct CAggregate
{
	int *m_pData;
};

template <typename t_CType>
t_CType *fg_Same(t_CType *_pValue)
{
	return _pValue;
}

struct CSelf
{
	~CSelf()
	{
		CAggregate Aggregate{Aggregate.m_pData};
		delete Aggregate.m_pData;
	}
};

void fg_TakeSelf(CSelf _Self)
{
}

struct CDesignated
{
	~CDesignated()
	{
		CAggregate Aggregate{.m_pData = Aggregate.m_pData};
		delete Aggregate.m_pData;
	}
};

void fg_TakeDesignated(CDesignated _Designated)
{
}

struct CThroughTemplate
{
	~CThroughTemplate()
	{
		CAggregate Aggregate{fg_Same(Aggregate.m_pData)};
		delete Aggregate.m_pData;
	}
};

void fg_TakeThroughTemplate(CThroughTemplate _ThroughTemplate)
{
}
