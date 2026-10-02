// RUN: %check_clang_tidy -std=c++23-or-later %s malterlib-thread-local-storage %t -- -config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml', malterlib-thread-local-storage.ThreadLocalTypes: 'CTimeThreadLocal;CQuietThreadLocal;CLambdaThreadLocal'}}" -- -I %S/Inputs

// What a generic list returns of what it is given, thread-local data only by
// the type a user gives it, is reported where the user's code makes the
// instantiation, the insert of `this` in the constructor of a thread-local
// type, not in the list; a NOLINT there silences it. Not a reference capture
// that is thread-local data whatever the arguments, which stays where it is
// captured, where its NOLINT is. Also what a lambda that isn't generic
// returns in an instantiation, of what it copies of a parameter of a type of
// the arguments, which is reported where the user's code makes the
// instantiation, not in the lambda.

#include "malterlib-concurrency.h"

using uint8 = unsigned char;
struct CListLink {
  CListLink *m_pNext = nullptr;
};
template <typename t_CData, typename t_CLink> struct TCIntrusiveList {
  static t_CLink *fsp_LinkFromMember(t_CData *_pMember) {
    return ((t_CLink *)(((uint8 *)_pMember) + 8));
  }
  void f_Insert(t_CData *_pData) { fp_Link(fsp_LinkFromMember(_pData)); }
  void fp_Link(t_CLink *_pLink) { m_pFirst = _pLink; }
  t_CLink *m_pFirst = nullptr;
};

struct CTimeThreadLocal {
  CTimeThreadLocal();
  CListLink m_Link;
};
struct CTimeInternal {
  TCIntrusiveList<CTimeThreadLocal, CListLink> m_ThreadLocals;
};
CTimeInternal *g_pTimeInternal;
CTimeThreadLocal::CTimeThreadLocal() {
  g_pTimeInternal->m_ThreadLocals.f_Insert(this);
// CHECK-MESSAGES: :[[@LINE-1]]:35: warning: a pointer to thread-local data is returned as a type that isn't a pointer to a thread-local type, so what it is given to can't tell it is thread-local data; return the thread-local type, or a copy [malterlib-thread-local-storage]
// CHECK-MESSAGES: :[[@LINE-2]]:44: warning: a pointer to thread-local data is given to 'f_Insert', which stores it where it can outlive the expression and be used on another thread; refer to thread-local data only from locals and parameters, and get it again where it is used [malterlib-thread-local-storage]
}

struct CQuietThreadLocal {
  CQuietThreadLocal();
  CListLink m_Link;
};
struct CQuietInternal {
  TCIntrusiveList<CQuietThreadLocal, CListLink> m_ThreadLocals;
};
CQuietInternal *g_pQuietInternal;
CQuietThreadLocal::CQuietThreadLocal() {
  g_pQuietInternal->m_ThreadLocals.f_Insert(this); // NOLINT(malterlib-thread-local-storage): the internal list is only used on this thread
}

thread_local int t_nRequests;
void fg_SaveRead(NMib::NFunction::TCFunction<int ()> const &_fRead);
template <typename t_CData> void fg_RegisterRead(t_CData &) {
  fg_SaveRead([&nRequests = t_nRequests] { return nRequests; }); // NOLINT(malterlib-thread-local-storage): read only on this thread
}
void fg_UseRegisterRead() {
  int Owner = 0;
  fg_RegisterRead(Owner);
}

struct CLambdaThreadLocal {
  CListLink m_Link;
};
template <typename t_CData> CListLink *fg_LambdaLink(t_CData *_pData) {
  return [_pData] { return &_pData->m_Link; }();
}
template <typename t_CData> CListLink *fg_NamedLambdaLink(t_CData *_pData) {
  auto fGet = [_pData]() -> CListLink * { return &_pData->m_Link; };
  return fGet();
}
void fg_UseLambdaLink(CLambdaThreadLocal *_pData) {
  (void)fg_LambdaLink(_pData);
// CHECK-MESSAGES: :[[@LINE-1]]:9: warning: a pointer to thread-local data is returned as a type that isn't a pointer to a thread-local type, so what it is given to can't tell it is thread-local data; return the thread-local type, or a copy [malterlib-thread-local-storage]
}
void fg_UseNamedLambdaLink(CLambdaThreadLocal *_pData) {
  (void)fg_NamedLambdaLink(_pData); // NOLINT(malterlib-thread-local-storage): the link is only used on this thread
}
