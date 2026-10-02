// The coroutine and actor checks on code that doesn't compile, as clang-tidy
// runs them: no crash, the errors reported and nothing else.
// RUN: not clang-tidy --checks='-*,malterlib-actor-*,malterlib-coroutine-*,malterlib-async-destroy,malterlib-coawait-*,malterlib-continue-running-on-actor,malterlib-runs-to-completion,malterlib-thread-local-storage' --config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" %s -- -std=c++20 -I %S/Inputs 2>&1 | FileCheck --implicit-check-not='Stack dump' --implicit-check-not='[malterlib-' --check-prefix=CHECK-ERRORS %s
// After an error that ends the compilation, a header that isn't found, the
// rest of the file is parsed without its diagnostics.
// RUN: not clang-tidy --checks='-*,malterlib-actor-*,malterlib-coroutine-*,malterlib-async-destroy,malterlib-coawait-*,malterlib-continue-running-on-actor,malterlib-runs-to-completion,malterlib-thread-local-storage' --config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" %s -- -std=c++20 -I %S/Inputs -DFATAL 2>&1 | FileCheck --implicit-check-not='Stack dump' --implicit-check-not='[malterlib-' --check-prefix=CHECK-FATAL %s

#include "malterlib-concurrency.h"

#ifdef FATAL
#include "malterlib-header-that-does-not-exist.h"
// CHECK-FATAL: error: 'malterlib-header-that-does-not-exist.h' file not found
#endif

TCFuture<int> fg_Suspend();

template <typename t_CType>
struct TCLink
{
	~TCLink()
	{
	}
	t_CType *m_pNext = nullptr;
};

struct CLogger
{
	void f_Pop(struct CScope &_Scope);
};

// An invalid record, its base not declared, whose destructor's body is
// checked.
struct CScope : public CMissingBase
{
	inline CScope(CLogger &_Logger, char const *_pName);
	inline ~CScope();
	void f_Suspend() noexcept override;

	CLogger &m_Logger;
	char const *m_pName;
	TCLink<CScope> m_Link;
	CMissingLink m_Missing;
};
// CHECK-ERRORS-DAG: error: expected class name
// CHECK-ERRORS-DAG: error: unknown type name 'CMissingLink'

CScope::~CScope()
{
	if (*m_pName)
		m_Logger.f_Pop(*this);
}

// An invalid declaration that a body uses.
CMissingJob *fg_FindJob(int _Index);
// CHECK-ERRORS-DAG: error: unknown type name 'CMissingJob'
int fg_Read(CMissingValue const &_Value);
// CHECK-ERRORS-DAG: error: unknown type name 'CMissingValue'

// Errors in bodies: a coroutine and a lambda in it, which use them.
TCFuture<int> fg_Run(int &_Value)
{
	auto *pJob = fg_FindJob(_Value);
	int *pValue = &_Value;
	co_await fg_Suspend();
	auto fRead = [&]
		{
			return fg_Missing(*pValue) + fg_Read(*pJob);
		}
	;
	CScope Scope(*pJob, "Run");
	co_return fRead() + fg_Read(*pJob) + fg_AlsoMissing();
}
// CHECK-ERRORS-DAG: error: use of undeclared identifier 'fg_Missing'
// CHECK-ERRORS-DAG: error: use of undeclared identifier 'fg_AlsoMissing'

// A member function of the invalid record, a coroutine.
struct CHolder : CScope
{
	TCFuture<int> f_Run()
	{
		co_await fg_Suspend();
		co_return *m_pName + m_Missing.f_Get();
	}
};
