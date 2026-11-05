// RUN: %check_clang_tidy -std=c++23-or-later %s malterlib-coroutine-thread-local-reference %t -- -config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml', malterlib-coroutine-thread-local-reference.ThreadLocalFunctions: 'NData::CProvider::operator NData::CData *;CLocal::f_Current;CPromiseThreadLocal::f_ConcurrencyThreadLocal'}}" -- -I %S/Inputs

#include "malterlib-actor-phase.h"

// ThreadLocalFunctions with a conversion to a qualified type, and accessors
// with an explicit object parameter.

namespace NData {
struct CData {
  int m_Value;
};
struct CProvider {
  operator NData::CData *();
};
} // namespace NData

namespace NThreadLocalOptions {
TCFuture<int> fg_Work();

struct CLocal {
  NData::CData &f_Current(this CLocal &_Self);
  NData::CData &f_Other(this CLocal &_Self);
};

struct CPromiseThreadLocal {
  NData::CData &f_ConcurrencyThreadLocal(this CPromiseThreadLocal &_Self);
};

NData::CProvider g_Provider;
CLocal g_Local;
CPromiseThreadLocal g_Promise;

TCFuture<int> fg_Conversion() {
  NData::CData *pData = g_Provider;
  co_await fg_Work();
  co_return pData->m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'pData' refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
}

TCFuture<int> fg_ExplicitObject() {
  auto &Data = g_Local.f_Current();
  co_await fg_Work();
  co_return Data.m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'Data' refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
}

TCFuture<int> fg_ExplicitObjectBuiltIn() {
  auto &Data = g_Promise.f_ConcurrencyThreadLocal();
  co_await fg_Work();
  co_return Data.m_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'Data' refers to thread-local data of the thread that ran before the suspension; the coroutine can resume on another thread; get it again after the suspension [malterlib-coroutine-thread-local-reference]
}

// Not another accessor.
TCFuture<int> fg_ExplicitObjectOther() {
  auto &Data = g_Local.f_Other();
  co_await fg_Work();
  co_return Data.m_Value;
}
} // namespace NThreadLocalOptions
