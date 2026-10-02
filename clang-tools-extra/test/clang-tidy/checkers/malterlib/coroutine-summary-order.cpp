// RUN: %check_clang_tidy -check-suffixes=LAMBDA -std=c++20-or-later %s malterlib-coroutine-lambda-call %t -- -config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" -- -I %S/Inputs
// RUN: %check_clang_tidy -check-suffixes=PARAMETER -std=c++20-or-later %s malterlib-coroutine-reference-parameter %t -- -config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" -- -I %S/Inputs
// RUN: %check_clang_tidy -check-suffixes=ACTOR -std=c++20-or-later %s malterlib-actor-dispatch-this %t -- -config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" -- -I %S/Inputs
// RUN: %check_clang_tidy -check-suffixes=THREAD -std=c++20-or-later %s malterlib-thread-local-storage %t -- -config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" -- -I %S/Inputs
// RUN: %check_clang_tidy -check-suffixes=RUNS -std=c++20-or-later %s malterlib-runs-to-completion %t -- -config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" -- -I %S/Inputs
// RUN: %check_clang_tidy -check-suffixes=OWNERSHIP -std=c++20-or-later %s malterlib-parameter-ownership %t -- -config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" -- -I %S/Inputs
// RUN: %check_clang_tidy -check-suffixes=CAPTURE -std=c++20-or-later %s malterlib-coroutine-reference-capture %t -- -config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" -- -I %S/Inputs
// RUN: %check_clang_tidy -check-suffixes=PHASE -std=c++20-or-later %s malterlib-actor-phase %t -- -config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" -- -I %S/Inputs

// Each summary is told the same wherever it is asked: a chain of helpers is
// followed to its end, however long it is, and what is checked after a long
// chain was worked out from its start is told as it is when it is checked
// first.

#include "malterlib-actor-phase.h"
#include "malterlib-concurrency.h"
#include "malterlib-runs-to-completion.h"

using namespace NMib::NConcurrency;

namespace NLambdaCall {
struct CText {
  char const *f_Get0() const { return f_Get1(); }
  char const *f_Get1() const { return f_Get2(); }
  char const *f_Get2() const { return f_Get3(); }
  char const *f_Get3() const { return f_Get4(); }
  char const *f_Get4() const { return f_Get5(); }
  char const *f_Get5() const { return f_Get6(); }
  char const *f_Get6() const { return f_Get7(); }
  char const *f_Get7() const { return f_Get8(); }
  char const *f_Get8() const { return f_Get9(); }
  char const *f_Get9() const { return f_Get10(); }
  char const *f_Get10() const { return f_Get11(); }
  char const *f_Get11() const { return f_Get12(); }
  char const *f_Get12() const { return f_Get13(); }
  char const *f_Get13() const { return f_Get14(); }
  char const *f_Get14() const { return f_Get15(); }
  char const *f_Get15() const { return f_Get16(); }
  char const *f_Get16() const { return "Name"; }
};
// What the getters give is a literal, through all of them.
void fg_Long() { (void)fg_CallSafe([Text = CText{}]() -> TCFuture<char const *> { co_return Text.f_Get0(); }); }
void fg_Short() { (void)fg_CallSafe([Text = CText{}]() -> TCFuture<char const *> { co_return Text.f_Get12(); }); }
} // namespace NLambdaCall

namespace NReferenceParameter {
struct CStrSpan {
  char const *m_pStr;
  unsigned m_Length;
};
char const *fg_EmptyName(CStrSpan _Text) { return "Default"; }
char const *fg_DefaultName(CStrSpan _Text) { return fg_EmptyName(_Text); }
char const *fg_DisplayName(CStrSpan _Text) { return fg_DefaultName(_Text); }
char const *fg_Get13(CStrSpan _Text) { return fg_DisplayName(_Text); }
char const *fg_Get12(CStrSpan _Text) { return fg_Get13(_Text); }
char const *fg_Get11(CStrSpan _Text) { return fg_Get12(_Text); }
char const *fg_Get10(CStrSpan _Text) { return fg_Get11(_Text); }
char const *fg_Get9(CStrSpan _Text) { return fg_Get10(_Text); }
char const *fg_Get8(CStrSpan _Text) { return fg_Get9(_Text); }
char const *fg_Get7(CStrSpan _Text) { return fg_Get8(_Text); }
char const *fg_Get6(CStrSpan _Text) { return fg_Get7(_Text); }
char const *fg_Get5(CStrSpan _Text) { return fg_Get6(_Text); }
char const *fg_Get4(CStrSpan _Text) { return fg_Get5(_Text); }
char const *fg_Get3(CStrSpan _Text) { return fg_Get4(_Text); }
char const *fg_Get2(CStrSpan _Text) { return fg_Get3(_Text); }
char const *fg_Get1(CStrSpan _Text) { return fg_Get2(_Text); }
char const *fg_Get0(CStrSpan _Text) { return fg_Get1(_Text); }
// What the helpers give is a literal, through all of them.
TCFuture<char const *> fg_Deep(CStrSpan _Text) { co_return fg_Get0(_Text); }
TCFuture<char const *> fg_ReadName(CStrSpan _Text) { co_return fg_DisplayName(_Text); }
} // namespace NReferenceParameter

namespace NActor {
bool fg_IsActor0(CActor const *_pActor);
bool fg_IsActor1(CActor const *_pActor);
bool fg_IsActor2(CActor const *_pActor);
bool fg_IsActor3(CActor const *_pActor);
bool fg_IsActor4(CActor const *_pActor);
bool fg_IsActor5(CActor const *_pActor);
bool fg_IsActor6(CActor const *_pActor);
bool fg_IsActor7(CActor const *_pActor);
bool fg_IsActor8(CActor const *_pActor);
bool fg_IsActor9(CActor const *_pActor);
bool fg_IsActor10(CActor const *_pActor);
bool fg_IsActor11(CActor const *_pActor);
bool fg_IsActor0(CActor const *_pActor) { return fg_IsActor1(_pActor); }
bool fg_IsActor1(CActor const *_pActor) { return fg_IsActor2(_pActor); }
bool fg_IsActor2(CActor const *_pActor) { return fg_IsActor3(_pActor); }
bool fg_IsActor3(CActor const *_pActor) { return fg_IsActor4(_pActor); }
bool fg_IsActor4(CActor const *_pActor) { return fg_IsActor5(_pActor); }
bool fg_IsActor5(CActor const *_pActor) { return fg_IsActor6(_pActor); }
bool fg_IsActor6(CActor const *_pActor) { return fg_IsActor7(_pActor); }
bool fg_IsActor7(CActor const *_pActor) { return fg_IsActor8(_pActor); }
bool fg_IsActor8(CActor const *_pActor) { return fg_IsActor9(_pActor); }
bool fg_IsActor9(CActor const *_pActor) { return fg_IsActor10(_pActor); }
bool fg_IsActor10(CActor const *_pActor) { return fg_IsActor11(_pActor); }
bool fg_IsActor11(CActor const *_pActor) { return _pActor != nullptr; }
CActor const *g_pOther;
// What only compares it, through three of them, after the longer chain was
// worked out from its start.
bool fg_Use() { return fg_IsActor9(g_pOther); }
} // namespace NActor

namespace NThreadLocalGives {
thread_local int g_Value;
int &fg_Data0();
int &fg_Data1();
int &fg_Data2();
int &fg_Data3();
int &fg_Data4();
int &fg_Data5();
int &fg_Data6();
int &fg_Data7();
int &fg_Data8();
int &fg_Data9();
int &fg_Data10();
int &fg_Data11();
int &fg_Data12();
int &fg_Data13();
int &fg_Data14();
int &fg_Data15();
int &fg_Data16();
int &fg_Data17();
int &fg_Data18();
int &fg_Data19();
int &fg_Data20();
int &fg_Data21();
int &fg_Data22();
int &fg_Data23();
int &fg_Data24();
int &fg_Data25();
int &fg_Data26();
int &fg_Data27();
int &fg_Data28();
int &fg_Data29();
int &fg_Data30();
int &fg_Data31();
int &fg_Data32();
int &fg_Data33();
int &fg_Data34();
int &fg_Data35();
int &fg_Data36();
int &fg_Data37();
int &fg_Data38();
int &fg_Data39();
int &fg_Data40();
// What the functions give is the thread-local data, through all of them.
int *g_pDeep;
void fg_Deep() { g_pDeep = &fg_Data0(); }
// CHECK-MESSAGES-THREAD: :[[@LINE-1]]:28: warning: a pointer to thread-local data is stored in the variable 'g_pDeep', which can outlive the expression and be used on another thread; {{.*}} [malterlib-thread-local-storage]
// NOLINTBEGIN(malterlib-thread-local-storage)
int &fg_Data0() { return fg_Data1(); }
int &fg_Data1() { return fg_Data2(); }
int &fg_Data2() { return fg_Data3(); }
int &fg_Data3() { return fg_Data4(); }
int &fg_Data4() { return fg_Data5(); }
int &fg_Data5() { return fg_Data6(); }
int &fg_Data6() { return fg_Data7(); }
int &fg_Data7() { return fg_Data8(); }
int &fg_Data8() { return fg_Data9(); }
int &fg_Data9() { return fg_Data10(); }
int &fg_Data10() { return fg_Data11(); }
int &fg_Data11() { return fg_Data12(); }
int &fg_Data12() { return fg_Data13(); }
int &fg_Data13() { return fg_Data14(); }
int &fg_Data14() { return fg_Data15(); }
int &fg_Data15() { return fg_Data16(); }
int &fg_Data16() { return fg_Data17(); }
int &fg_Data17() { return fg_Data18(); }
int &fg_Data18() { return fg_Data19(); }
int &fg_Data19() { return fg_Data20(); }
int &fg_Data20() { return fg_Data21(); }
int &fg_Data21() { return fg_Data22(); }
int &fg_Data22() { return fg_Data23(); }
int &fg_Data23() { return fg_Data24(); }
int &fg_Data24() { return fg_Data25(); }
int &fg_Data25() { return fg_Data26(); }
int &fg_Data26() { return fg_Data27(); }
int &fg_Data27() { return fg_Data28(); }
int &fg_Data28() { return fg_Data29(); }
int &fg_Data29() { return fg_Data30(); }
int &fg_Data30() { return fg_Data31(); }
int &fg_Data31() { return fg_Data32(); }
int &fg_Data32() { return fg_Data33(); }
int &fg_Data33() { return fg_Data34(); }
int &fg_Data34() { return fg_Data35(); }
int &fg_Data35() { return fg_Data36(); }
int &fg_Data36() { return fg_Data37(); }
int &fg_Data37() { return fg_Data38(); }
int &fg_Data38() { return fg_Data39(); }
int &fg_Data39() { return fg_Data40(); }
int &fg_Data40() { return g_Value; }
// NOLINTEND(malterlib-thread-local-storage)
// Ten of them, after the longer chain was worked out from its start.
int *g_pShallow;
void fg_Shallow() { g_pShallow = &fg_Data30(); }
// CHECK-MESSAGES-THREAD: :[[@LINE-1]]:34: warning: a pointer to thread-local data is stored in the variable 'g_pShallow', which can outlive the expression and be used on another thread; {{.*}} [malterlib-thread-local-storage]
} // namespace NThreadLocalGives

namespace NThreadLocalStores {
thread_local int g_Stored;
int *g_pKept;
template <int t_iLink> void fg_Keep(int *_pValue) { fg_Keep<t_iLink + 1>(_pValue); }
template <> void fg_Keep<12>(int *_pValue) { g_pKept = _pValue; }
// What the last link keeps, through all of them.
void fg_Deep() { fg_Keep<0>(&g_Stored); }
// CHECK-MESSAGES-THREAD: :[[@LINE-1]]:29: warning: a pointer to thread-local data is given to 'fg_Keep<0>', which stores it where it can outlive {{.*}} [malterlib-thread-local-storage]
// Four of them, after the longer chain was worked out from its start.
void fg_Shallow() { fg_Keep<8>(&g_Stored); }
// CHECK-MESSAGES-THREAD: :[[@LINE-1]]:32: warning: a pointer to thread-local data is given to 'fg_Keep<8>', which stores it where it can outlive {{.*}} [malterlib-thread-local-storage]
} // namespace NThreadLocalStores

namespace NRunsToCompletion {
TCFuture<int> fg_Work(int _Value);
DMibRunsToCompletion TCFuture<int> fg_Read(int &_Value);
using CWatch = decltype(fg_OnResume([]() -> CExceptionPointer { return {}; }));
CWatch fg_OtherWatch();
CWatch fg_Watch70() { return fg_OtherWatch(); }
CWatch fg_Watch69() { return fg_Watch70(); }
CWatch fg_Watch68() { return fg_Watch69(); }
CWatch fg_Watch67() { return fg_Watch68(); }
CWatch fg_Watch66() { return fg_Watch67(); }
CWatch fg_Watch65() { return fg_Watch66(); }
CWatch fg_Watch64() { return fg_Watch65(); }
CWatch fg_Watch63() { return fg_Watch64(); }
CWatch fg_Watch62() { return fg_Watch63(); }
CWatch fg_Watch61() { return fg_Watch62(); }
CWatch fg_Watch60() { return fg_Watch61(); }
CWatch fg_Watch59() { return fg_Watch60(); }
CWatch fg_Watch58() { return fg_Watch59(); }
CWatch fg_Watch57() { return fg_Watch58(); }
CWatch fg_Watch56() { return fg_Watch57(); }
CWatch fg_Watch55() { return fg_Watch56(); }
CWatch fg_Watch54() { return fg_Watch55(); }
CWatch fg_Watch53() { return fg_Watch54(); }
CWatch fg_Watch52() { return fg_Watch53(); }
CWatch fg_Watch51() { return fg_Watch52(); }
CWatch fg_Watch50() { return fg_Watch51(); }
CWatch fg_Watch49() { return fg_Watch50(); }
CWatch fg_Watch48() { return fg_Watch49(); }
CWatch fg_Watch47() { return fg_Watch48(); }
CWatch fg_Watch46() { return fg_Watch47(); }
CWatch fg_Watch45() { return fg_Watch46(); }
CWatch fg_Watch44() { return fg_Watch45(); }
CWatch fg_Watch43() { return fg_Watch44(); }
CWatch fg_Watch42() { return fg_Watch43(); }
CWatch fg_Watch41() { return fg_Watch42(); }
CWatch fg_Watch40() { return fg_Watch41(); }
CWatch fg_Watch39() { return fg_Watch40(); }
CWatch fg_Watch38() { return fg_Watch39(); }
CWatch fg_Watch37() { return fg_Watch38(); }
CWatch fg_Watch36() { return fg_Watch37(); }
CWatch fg_Watch35() { return fg_Watch36(); }
CWatch fg_Watch34() { return fg_Watch35(); }
CWatch fg_Watch33() { return fg_Watch34(); }
CWatch fg_Watch32() { return fg_Watch33(); }
CWatch fg_Watch31() { return fg_Watch32(); }
CWatch fg_Watch30() { return fg_Watch31(); }
CWatch fg_Watch29() { return fg_Watch30(); }
CWatch fg_Watch28() { return fg_Watch29(); }
CWatch fg_Watch27() { return fg_Watch28(); }
CWatch fg_Watch26() { return fg_Watch27(); }
CWatch fg_Watch25() { return fg_Watch26(); }
CWatch fg_Watch24() { return fg_Watch25(); }
CWatch fg_Watch23() { return fg_Watch24(); }
CWatch fg_Watch22() { return fg_Watch23(); }
CWatch fg_Watch21() { return fg_Watch22(); }
CWatch fg_Watch20() { return fg_Watch21(); }
CWatch fg_Watch19() { return fg_Watch20(); }
CWatch fg_Watch18() { return fg_Watch19(); }
CWatch fg_Watch17() { return fg_Watch18(); }
CWatch fg_Watch16() { return fg_Watch17(); }
CWatch fg_Watch15() { return fg_Watch16(); }
CWatch fg_Watch14() { return fg_Watch15(); }
CWatch fg_Watch13() { return fg_Watch14(); }
CWatch fg_Watch12() { return fg_Watch13(); }
CWatch fg_Watch11() { return fg_Watch12(); }
CWatch fg_Watch10() { return fg_Watch11(); }
CWatch fg_Watch9() { return fg_Watch10(); }
CWatch fg_Watch8() { return fg_Watch9(); }
CWatch fg_Watch7() { return fg_Watch8(); }
CWatch fg_Watch6() { return fg_Watch7(); }
CWatch fg_Watch5() { return fg_Watch6(); }
CWatch fg_Watch4() { return fg_Watch5(); }
CWatch fg_Watch3() { return fg_Watch4(); }
CWatch fg_Watch2() { return fg_Watch3(); }
CWatch fg_Watch1() { return fg_Watch2(); }
CWatch fg_Watch0() { return fg_Watch1(); }
// What is awaited is what a function that can't be looked into gives,
// through all of them, which registers no callback on resume.
DMibRunsToCompletion TCFuture<int> fg_Deep() {
  auto Watch = co_await fg_Watch0();
  TCFutureVector<int> Results;
  int Local = 42;
  auto Guard = co_await fg_AsyncDestroy([&]() -> TCFuture<int> {
    co_await fg_AllDone(Results).f_Wrap();
    co_return 0;
  });
  fg_Read(Local) > Results;
  co_await fg_Work(0).f_Wrap();
  co_await fg_Move(Guard);
  co_return 0;
}
// Thirty of them, after the longer chain was worked out from its start.
DMibRunsToCompletion TCFuture<int> fg_Shallow() {
  auto Watch = co_await fg_Watch40();
  TCFutureVector<int> Results;
  int Local = 42;
  auto Guard = co_await fg_AsyncDestroy([&]() -> TCFuture<int> {
    co_await fg_AllDone(Results).f_Wrap();
    co_return 0;
  });
  fg_Read(Local) > Results;
  co_await fg_Work(0).f_Wrap();
  co_await fg_Move(Guard);
  co_return 0;
}
} // namespace NRunsToCompletion

namespace NParameterOwnership {
void fg_Take(int *&o_pValue, int *_pOther) { o_pValue = _pOther; }
struct CBase {
  int *m_pData;
  int *f_Get6() { return new int(*m_pData); }
  int *f_Get5() { return f_Get6(); }
  int *f_Get4() { return f_Get5(); }
  int *f_Get3() { return f_Get4(); }
  int *f_Get2() {
    int *pTaken = nullptr;
    fg_Take(pTaken, f_Get3());
    return pTaken;
  }
  int *f_Get1() { return f_Get2(); }
  int *f_Get0() { return f_Get1(); }
};
// Through each getter, however many, what the last gives is a copy, which
// the destructor deletes, not what the object owns.
struct CDeep : CBase {
  CDeep(CDeep const &) = delete;
  ~CDeep() { delete f_Get0(); }
};
void fg_UseDeep(CDeep _Owner) {}
// Through fewer of them, after the longer chain was worked out from its
// start, what is taken is a copy too.
struct CShallow : CBase {
  CShallow(CShallow const &) = delete;
  ~CShallow() { delete f_Get2(); }
};
void fg_UseShallow(CShallow _Owner) {}
// Reported: through as many getters, what the last gives is what the object
// owns, which the destructor deletes.
struct COwnBase {
  int *m_pData;
  int *f_Own6() { return m_pData; }
  int *f_Own5() { return f_Own6(); }
  int *f_Own4() { return f_Own5(); }
  int *f_Own3() { return f_Own4(); }
  int *f_Own2() {
    int *pTaken = nullptr;
    fg_Take(pTaken, f_Own3());
    return pTaken;
  }
  int *f_Own1() { return f_Own2(); }
  int *f_Own0() { return f_Own1(); }
};
struct CDeepOwn : COwnBase {
  CDeepOwn(CDeepOwn const &) = delete;
  ~CDeepOwn() { delete f_Own0(); }
};
void fg_UseDeepOwn(CDeepOwn _Owner) {}
// CHECK-MESSAGES-OWNERSHIP: :[[@LINE-1]]:29: warning: parameter '_Owner' takes 'CDeepOwn' by value; take it by 'const &' when the function borrows it or by '&&' when it consumes it [malterlib-parameter-ownership]
} // namespace NParameterOwnership

namespace NReferenceCapture {
TCFuture<int> fg_Work();
struct CVector {
  int m_Data[4];
  ~CVector();
  int *f_GetArray() { return m_Data; }
};
int *fg_Begin6(CVector &_Values) { return _Values.f_GetArray(); }
int *fg_Begin5(CVector &_Values) { return fg_Begin6(_Values); }
int *fg_Begin4(CVector &_Values) { return fg_Begin5(_Values); }
int *fg_Begin3(CVector &_Values) { return fg_Begin4(_Values); }
int *fg_Begin2(CVector &_Values) { return fg_Begin3(_Values); }
int *fg_Begin1(CVector &_Values) { return fg_Begin2(_Values); }
int *fg_Begin0(CVector &_Values) { return fg_Begin1(_Values); }
// What the helpers point into is what they are given, through all of them.
TCFuture<int> fg_Deep() {
  CVector Values;
  co_await fg_CallSafe([p = fg_Begin0(Values)]() -> TCFuture<int> {
// CHECK-MESSAGES-CAPTURE: :[[@LINE-1]]:25: warning: a TCFuture coroutine lambda uses 'p', which points to 'Values', after a suspension, when 'Values' may be gone; capture a value or a shared pointer, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
    co_await fg_Work();
    co_return *p;
  });
  co_return 0;
}
// Four of them, after the longer chain was worked out from its start.
TCFuture<int> fg_Shallow() {
  CVector Values;
  co_await fg_CallSafe([p = fg_Begin3(Values)]() -> TCFuture<int> {
// CHECK-MESSAGES-CAPTURE: :[[@LINE-1]]:25: warning: a TCFuture coroutine lambda uses 'p', which points to 'Values', after a suspension, when 'Values' may be gone; capture a value or a shared pointer, or return a TCUnsafeFuture [malterlib-coroutine-reference-capture]
    co_await fg_Work();
    co_return *p;
  });
  co_return 0;
}
} // namespace NReferenceCapture

namespace NActorPhase {
template <int t_iLink> struct TCLink {
  TCLink<t_iLink + 1> m_Next;
  ~TCLink() {}
};
template <> struct TCLink<300> {
  int m_Value = 0;
  ~TCLink() {}
};
struct CState {
  DMibActorEntries(Destroy) TCMap<int, int> m_Items;
};
template <int t_iLink> struct TCObserver {
  CState *m_pState = nullptr;
  TCLink<t_iLink> m_Link;
  ~TCObserver() {}
  CState *operator->() const { return m_pState; }
};
// Destroying one releases nothing, through all of its members.
struct CDeep : CActor {
  TCObserver<0> m_Observer;
  void f_Forget() { m_Observer = TCObserver<0>{}; }
};
// A hundred of them, after the longer chain was worked out from its start.
struct CShallow : CActor {
  TCObserver<200> m_Observer;
  void f_Forget() { m_Observer = TCObserver<200>{}; }
};
} // namespace NActorPhase

namespace NWriteTargets {
// What a helper's write through a pointer to its local designates, worked out
// first for a coroutine that reaches the helper through fourteen others, then
// for one that calls it: neither gives what it is given, which the helper
// points elsewhere before it returns it.
int *fg_Clear(int *_pValue) {
  int *p = _pValue;
  int **pp = &p;
  *pp = nullptr;
  return p;
}
int *fg_Link13(int *_pValue) { return fg_Clear(_pValue); }
int *fg_Link12(int *_pValue) { return fg_Link13(_pValue); }
int *fg_Link11(int *_pValue) { return fg_Link12(_pValue); }
int *fg_Link10(int *_pValue) { return fg_Link11(_pValue); }
int *fg_Link9(int *_pValue) { return fg_Link10(_pValue); }
int *fg_Link8(int *_pValue) { return fg_Link9(_pValue); }
int *fg_Link7(int *_pValue) { return fg_Link8(_pValue); }
int *fg_Link6(int *_pValue) { return fg_Link7(_pValue); }
int *fg_Link5(int *_pValue) { return fg_Link6(_pValue); }
int *fg_Link4(int *_pValue) { return fg_Link5(_pValue); }
int *fg_Link3(int *_pValue) { return fg_Link4(_pValue); }
int *fg_Link2(int *_pValue) { return fg_Link3(_pValue); }
int *fg_Link1(int *_pValue) { return fg_Link2(_pValue); }
int *fg_Link0(int *_pValue) { return fg_Link1(_pValue); }
TCFuture<int *> fg_Deep(int *_pValue) { co_return fg_Link0(_pValue); }
TCFuture<int *> fg_Shallow(int *_pValue) { co_return fg_Clear(_pValue); }
// What the same shape gives when it points it at what it is given again.
int *fg_Keep(int *_pValue) {
  int *p = nullptr;
  int **pp = &p;
  *pp = _pValue;
  return p;
}
TCFuture<int *> fg_Kept(int *_pValue) { co_return fg_Keep(_pValue); }
// CHECK-MESSAGES-PARAMETER: :[[@LINE-1]]:30: warning: TCFuture coroutine 'fg_Kept' uses '_pValue' after a suspension, but it takes it as 'int *', which refers to data the caller owns, which can be gone by then; use it only before the first suspension, take the data by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-parameter]
} // namespace NWriteTargets

namespace NWriteTargetsShallowFirst {
// The same, the coroutine that calls the helper checked first.
int *fg_Clear(int *_pValue) {
  int *p = _pValue;
  int **pp = &p;
  *pp = nullptr;
  return p;
}
int *fg_Link13(int *_pValue) { return fg_Clear(_pValue); }
int *fg_Link12(int *_pValue) { return fg_Link13(_pValue); }
int *fg_Link11(int *_pValue) { return fg_Link12(_pValue); }
int *fg_Link10(int *_pValue) { return fg_Link11(_pValue); }
int *fg_Link9(int *_pValue) { return fg_Link10(_pValue); }
int *fg_Link8(int *_pValue) { return fg_Link9(_pValue); }
int *fg_Link7(int *_pValue) { return fg_Link8(_pValue); }
int *fg_Link6(int *_pValue) { return fg_Link7(_pValue); }
int *fg_Link5(int *_pValue) { return fg_Link6(_pValue); }
int *fg_Link4(int *_pValue) { return fg_Link5(_pValue); }
int *fg_Link3(int *_pValue) { return fg_Link4(_pValue); }
int *fg_Link2(int *_pValue) { return fg_Link3(_pValue); }
int *fg_Link1(int *_pValue) { return fg_Link2(_pValue); }
int *fg_Link0(int *_pValue) { return fg_Link1(_pValue); }
TCFuture<int *> fg_Shallow(int *_pValue) { co_return fg_Clear(_pValue); }
TCFuture<int *> fg_Deep(int *_pValue) { co_return fg_Link0(_pValue); }
// What the same shape gives when it points it at what it is given again.
int *fg_Keep(int *_pValue) {
  int *p = nullptr;
  int **pp = &p;
  *pp = _pValue;
  return p;
}
TCFuture<int *> fg_Kept(int *_pValue) { co_return fg_Keep(_pValue); }
// CHECK-MESSAGES-PARAMETER: :[[@LINE-1]]:30: warning: TCFuture coroutine 'fg_Kept' uses '_pValue' after a suspension, but it takes it as 'int *', which refers to data the caller owns, which can be gone by then; use it only before the first suspension, take the data by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-parameter]
} // namespace NWriteTargetsShallowFirst

namespace NAliasWrites {
// The same through a local reference to the local, whose facts the deep
// coroutine asks first, from as deep a walk.
int *fg_Clear(int *_pValue) {
  int *p = _pValue;
  int *&rp = p;
  rp = nullptr;
  return p;
}
int *fg_Link13(int *_pValue) { return fg_Clear(_pValue); }
int *fg_Link12(int *_pValue) { return fg_Link13(_pValue); }
int *fg_Link11(int *_pValue) { return fg_Link12(_pValue); }
int *fg_Link10(int *_pValue) { return fg_Link11(_pValue); }
int *fg_Link9(int *_pValue) { return fg_Link10(_pValue); }
int *fg_Link8(int *_pValue) { return fg_Link9(_pValue); }
int *fg_Link7(int *_pValue) { return fg_Link8(_pValue); }
int *fg_Link6(int *_pValue) { return fg_Link7(_pValue); }
int *fg_Link5(int *_pValue) { return fg_Link6(_pValue); }
int *fg_Link4(int *_pValue) { return fg_Link5(_pValue); }
int *fg_Link3(int *_pValue) { return fg_Link4(_pValue); }
int *fg_Link2(int *_pValue) { return fg_Link3(_pValue); }
int *fg_Link1(int *_pValue) { return fg_Link2(_pValue); }
int *fg_Link0(int *_pValue) { return fg_Link1(_pValue); }
TCFuture<int *> fg_Deep(int *_pValue) { co_return fg_Link0(_pValue); }
TCFuture<int *> fg_Shallow(int *_pValue) { co_return fg_Clear(_pValue); }
// What the same shape gives when it points it at what it is given again.
int *fg_Keep(int *_pValue) {
  int *p = nullptr;
  int *&rp = p;
  rp = _pValue;
  return p;
}
TCFuture<int *> fg_Kept(int *_pValue) { co_return fg_Keep(_pValue); }
// CHECK-MESSAGES-PARAMETER: :[[@LINE-1]]:30: warning: TCFuture coroutine 'fg_Kept' uses '_pValue' after a suspension, but it takes it as 'int *', which refers to data the caller owns, which can be gone by then; use it only before the first suspension, take the data by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-parameter]
} // namespace NAliasWrites

namespace NAliasWritesShallowFirst {
// The same, the coroutine that calls the helper checked first.
int *fg_Clear(int *_pValue) {
  int *p = _pValue;
  int *&rp = p;
  rp = nullptr;
  return p;
}
int *fg_Link13(int *_pValue) { return fg_Clear(_pValue); }
int *fg_Link12(int *_pValue) { return fg_Link13(_pValue); }
int *fg_Link11(int *_pValue) { return fg_Link12(_pValue); }
int *fg_Link10(int *_pValue) { return fg_Link11(_pValue); }
int *fg_Link9(int *_pValue) { return fg_Link10(_pValue); }
int *fg_Link8(int *_pValue) { return fg_Link9(_pValue); }
int *fg_Link7(int *_pValue) { return fg_Link8(_pValue); }
int *fg_Link6(int *_pValue) { return fg_Link7(_pValue); }
int *fg_Link5(int *_pValue) { return fg_Link6(_pValue); }
int *fg_Link4(int *_pValue) { return fg_Link5(_pValue); }
int *fg_Link3(int *_pValue) { return fg_Link4(_pValue); }
int *fg_Link2(int *_pValue) { return fg_Link3(_pValue); }
int *fg_Link1(int *_pValue) { return fg_Link2(_pValue); }
int *fg_Link0(int *_pValue) { return fg_Link1(_pValue); }
TCFuture<int *> fg_Shallow(int *_pValue) { co_return fg_Clear(_pValue); }
TCFuture<int *> fg_Deep(int *_pValue) { co_return fg_Link0(_pValue); }
// What the same shape gives when it points it at what it is given again.
int *fg_Keep(int *_pValue) {
  int *p = nullptr;
  int *&rp = p;
  rp = _pValue;
  return p;
}
TCFuture<int *> fg_Kept(int *_pValue) { co_return fg_Keep(_pValue); }
// CHECK-MESSAGES-PARAMETER: :[[@LINE-1]]:30: warning: TCFuture coroutine 'fg_Kept' uses '_pValue' after a suspension, but it takes it as 'int *', which refers to data the caller owns, which can be gone by then; use it only before the first suspension, take the data by value, or return a TCUnsafeFuture [malterlib-coroutine-reference-parameter]
} // namespace NAliasWritesShallowFirst
