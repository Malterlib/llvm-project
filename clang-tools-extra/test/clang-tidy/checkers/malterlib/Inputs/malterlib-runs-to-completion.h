#pragma once

#include "malterlib-concurrency.h"

// The annotations of Mib/Concurrency/TidyAnnotations.
#define DMibRunsToCompletion [[clang::annotate("malterlib-runs-to-completion")]]
#define DMibRunsToCompletionEntry [[clang::annotate("malterlib-runs-to-completion-entry")]]

namespace NMib::NConcurrency
{
	TCActor<> fg_ConcurrentActor();
	TCActor<> fg_ConcurrentActorLowPrio();
	TCActor<> const &fg_ThisConcurrentActor();
	TCActor<> const &fg_DynamicConcurrentActorLowPrio();

	template <typename t_CType>
	struct TCFutureVector
	{
	};

	template <typename t_CType>
	void operator > (TCFuture<t_CType> &&_Future, TCFutureVector<t_CType> &_Results);

	template <typename t_CType>
	TCFuture<int> fg_AllDone(TCFutureVector<t_CType> &_Results);
}
