// RUN: %check_clang_tidy -std=c++20-or-later %s malterlib-coroutine-reference-capture %t -- \
// RUN:   -config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml', OwnerTypes: 'TCSharedPointer=shared;TCWeakPointer=weak;TCUniquePointer=unique;CContextPointer=shared'}}" \
// RUN:   -- -I %S/Inputs

#include "malterlib-concurrency.h"

// An owner of OwnerTypes that isn't a template, whose owned type can't be
// told by its arguments, `CContextPointer=shared`: its anchor is the owner
// type itself, and a borrow through it and the job's owner is kept by the
// captured owner of the job.
namespace NReview2Round175NonTemplateOwner {
using namespace NMib::NStorage;
TCFuture<int> fg_Work();
struct CJob { int m_Result = 0; };
struct CContext { TCSharedPointer<CJob> m_pJob; };
struct CContextPointer {
  CContext *operator->() const;
  CContext &operator*() const;
  CContext *mp_pContext;
};
TCSharedPointer<CJob> fg_GetJob(CContextPointer const &_pContext) { return _pContext->m_pJob; }
TCFuture<int> fg_Run(CContextPointer _pContext) {
  int &Result = _pContext->m_pJob->m_Result;
  return fg_CallSafe([&Result, pJob = fg_GetJob(_pContext)]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });
// Clean: CContextPointer is a shared owner by this test's OwnerTypes, _pContext is a by-value parameter nothing writes, and pJob = fg_GetJob(_pContext) is a copy of the m_pJob that owns the job Result points into, so the capture keeps it.
}
} // namespace NReview2Round175NonTemplateOwner
