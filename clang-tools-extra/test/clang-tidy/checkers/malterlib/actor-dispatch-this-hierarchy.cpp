// RUN: %check_clang_tidy -std=c++23-or-later %s malterlib-actor-dispatch-this %t -- -config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" -- -I %S/Inputs

// A copy of a view of a hierarchy whose bases are shared by many paths, each
// class virtually deriving from both of the level before, told once for each
// class, not once for each path.

#include "malterlib-concurrency.h"

struct CBase {
  int *m_pValue;
};
struct CA0 : virtual CBase {};
struct CB0 : virtual CBase {};
struct CA1 : virtual CA0, virtual CB0 {};
struct CB1 : virtual CA0, virtual CB0 {};
struct CA2 : virtual CA1, virtual CB1 {};
struct CB2 : virtual CA1, virtual CB1 {};
struct CA3 : virtual CA2, virtual CB2 {};
struct CB3 : virtual CA2, virtual CB2 {};
struct CA4 : virtual CA3, virtual CB3 {};
struct CB4 : virtual CA3, virtual CB3 {};
struct CA5 : virtual CA4, virtual CB4 {};
struct CB5 : virtual CA4, virtual CB4 {};
struct CA6 : virtual CA5, virtual CB5 {};
struct CB6 : virtual CA5, virtual CB5 {};
struct CA7 : virtual CA6, virtual CB6 {};
struct CB7 : virtual CA6, virtual CB6 {};
struct CA8 : virtual CA7, virtual CB7 {};
struct CB8 : virtual CA7, virtual CB7 {};
struct CA9 : virtual CA8, virtual CB8 {};
struct CB9 : virtual CA8, virtual CB8 {};
struct CA10 : virtual CA9, virtual CB9 {};
struct CB10 : virtual CA9, virtual CB9 {};
struct CA11 : virtual CA10, virtual CB10 {};
struct CB11 : virtual CA10, virtual CB10 {};
struct CA12 : virtual CA11, virtual CB11 {};
struct CB12 : virtual CA11, virtual CB11 {};
struct CA13 : virtual CA12, virtual CB12 {};
struct CB13 : virtual CA12, virtual CB12 {};
struct CA14 : virtual CA13, virtual CB13 {};
struct CB14 : virtual CA13, virtual CB13 {};
struct CA15 : virtual CA14, virtual CB14 {};
struct CB15 : virtual CA14, virtual CB14 {};
struct CA16 : virtual CA15, virtual CB15 {};
struct CB16 : virtual CA15, virtual CB15 {};
struct CA17 : virtual CA16, virtual CB16 {};
struct CB17 : virtual CA16, virtual CB16 {};
struct CA18 : virtual CA17, virtual CB17 {};
struct CB18 : virtual CA17, virtual CB17 {};
struct CA19 : virtual CA18, virtual CB18 {};
struct CB19 : virtual CA18, virtual CB18 {};
struct CA20 : virtual CA19, virtual CB19 {};
struct CB20 : virtual CA19, virtual CB19 {};
struct CA21 : virtual CA20, virtual CB20 {};
struct CB21 : virtual CA20, virtual CB20 {};
struct CA22 : virtual CA21, virtual CB21 {};
struct CB22 : virtual CA21, virtual CB21 {};
struct CA23 : virtual CA22, virtual CB22 {};
struct CB23 : virtual CA22, virtual CB22 {};
struct CA24 : virtual CA23, virtual CB23 {};
struct CB24 : virtual CA23, virtual CB23 {};
struct CA25 : virtual CA24, virtual CB24 {};
struct CB25 : virtual CA24, virtual CB24 {};
struct CA26 : virtual CA25, virtual CB25 {};
struct CB26 : virtual CA25, virtual CB25 {};
struct CA27 : virtual CA26, virtual CB26 {};
struct CB27 : virtual CA26, virtual CB26 {};
struct CA28 : virtual CA27, virtual CB27 {};
struct CB28 : virtual CA27, virtual CB27 {};
struct CA29 : virtual CA28, virtual CB28 {};
struct CB29 : virtual CA28, virtual CB28 {};
struct CA30 : virtual CA29, virtual CB29 {};
struct CB30 : virtual CA29, virtual CB29 {};
struct CA31 : virtual CA30, virtual CB30 {};
struct CB31 : virtual CA30, virtual CB30 {};
struct CA32 : virtual CA31, virtual CB31 {};
struct CB32 : virtual CA31, virtual CB31 {};
struct CA33 : virtual CA32, virtual CB32 {};
struct CB33 : virtual CA32, virtual CB32 {};
struct CA34 : virtual CA33, virtual CB33 {};
struct CB34 : virtual CA33, virtual CB33 {};
struct CA35 : virtual CA34, virtual CB34 {};
struct CB35 : virtual CA34, virtual CB34 {};
struct CA36 : virtual CA35, virtual CB35 {};
struct CB36 : virtual CA35, virtual CB35 {};
struct CA37 : virtual CA36, virtual CB36 {};
struct CB37 : virtual CA36, virtual CB36 {};
struct CA38 : virtual CA37, virtual CB37 {};
struct CB38 : virtual CA37, virtual CB37 {};
struct CA39 : virtual CA38, virtual CB38 {};
struct CB39 : virtual CA38, virtual CB38 {};
struct CState : CActor {
  CA39 m_View;
  TCActor<> m_Other;
  TCFuture<int> f_Read() {
    auto Copy = m_View;
    co_return co_await (g_Dispatch(m_Other) / [&Copy] { return *Copy.m_pValue; });
// CHECK-MESSAGES: :[[@LINE-1]]:49: warning: a lambda that captures 'this' of actor 'CState' runs on another actor, where using the object races with its own actor; capture copies of what it needs instead [malterlib-actor-dispatch-this]
  }
};
