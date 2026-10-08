// RUN: %clang_cc1 -fsyntax-only -std=c++20 -verify -verify-ignore-unexpected=note -I %S/Inputs %s

// The default argument of each function in this chain uses the one before it
// twice, so its expression, which each use shares, is reached 2^40 times
// through the uses: the uses are checked in time linear in the length of the
// chain, not exponential.
int g(int, int);
int f0(int = 0);
int f1(int = g(f0(), f0()));
int f2(int = g(f1(), f1()));
int f3(int = g(f2(), f2()));
int f4(int = g(f3(), f3()));
int f5(int = g(f4(), f4()));
int f6(int = g(f5(), f5()));
int f7(int = g(f6(), f6()));
int f8(int = g(f7(), f7()));
int f9(int = g(f8(), f8()));
int f10(int = g(f9(), f9()));
int f11(int = g(f10(), f10()));
int f12(int = g(f11(), f11()));
int f13(int = g(f12(), f12()));
int f14(int = g(f13(), f13()));
int f15(int = g(f14(), f14()));
int f16(int = g(f15(), f15()));
int f17(int = g(f16(), f16()));
int f18(int = g(f17(), f17()));
int f19(int = g(f18(), f18()));
int f20(int = g(f19(), f19()));
int f21(int = g(f20(), f20()));
int f22(int = g(f21(), f21()));
int f23(int = g(f22(), f22()));
int f24(int = g(f23(), f23()));
int f25(int = g(f24(), f24()));
int f26(int = g(f25(), f25()));
int f27(int = g(f26(), f26()));
int f28(int = g(f27(), f27()));
int f29(int = g(f28(), f28()));
int f30(int = g(f29(), f29()));
int f31(int = g(f30(), f30()));
int f32(int = g(f31(), f31()));
int f33(int = g(f32(), f32()));
int f34(int = g(f33(), f33()));
int f35(int = g(f34(), f34()));
int f36(int = g(f35(), f35()));
int f37(int = g(f36(), f36()));
int f38(int = g(f37(), f37()));
int f39(int = g(f38(), f38()));
int f40(int = g(f39(), f39()));
int use() { return f40(); }

// And an immediate invocation at the start of such a chain is still found
// where the default argument that has it is used, in the default arguments
// rewritten for the use, which the uses in them share, once.
consteval int Fail(int x) { return 1 / x; }
int h0(int = Fail(0)); // expected-error {{call to consteval function 'Fail' is not a constant expression}}
int h1(int = g(h0(), h0()));
int h2(int = g(h1(), h1()));
int useFail() { return h2(); }

// So is one whose default argument uses the one before it in arithmetic,
// which the check for overflow evaluates, and checks each default argument in
// once.
int a0(int = 0);
int a1(int = a0() + a0());
int a2(int = a1() + a1());
int a3(int = a2() + a2());
int a4(int = a3() + a3());
int a5(int = a4() + a4());
int a6(int = a5() + a5());
int a7(int = a6() + a6());
int a8(int = a7() + a7());
int a9(int = a8() + a8());
int a10(int = a9() + a9());
int a11(int = a10() + a10());
int a12(int = a11() + a11());
int a13(int = a12() + a12());
int a14(int = a13() + a13());
int a15(int = a14() + a14());
int a16(int = a15() + a15());
int a17(int = a16() + a16());
int a18(int = a17() + a17());
int a19(int = a18() + a18());
int a20(int = a19() + a19());
int a21(int = a20() + a20());
int a22(int = a21() + a21());
int a23(int = a22() + a22());
int a24(int = a23() + a23());
int a25(int = a24() + a24());
int a26(int = a25() + a25());
int a27(int = a26() + a26());
int a28(int = a27() + a27());
int a29(int = a28() + a28());
int a30(int = a29() + a29());
int a31(int = a30() + a30());
int a32(int = a31() + a31());
int a33(int = a32() + a32());
int a34(int = a33() + a33());
int a35(int = a34() + a34());
int a36(int = a35() + a35());
int a37(int = a36() + a36());
int a38(int = a37() + a37());
int a39(int = a38() + a38());
int a40(int = a39() + a39());
int useArithmetic() { return a40(); }

// And an overflow in a default argument is found where it is declared, and
// once by each check that reaches it.
int o0(int = __INT_MAX__ + 1); // expected-warning 3 {{overflow in expression; result is}}
int o1(int = o0() + o0());
int o2(int = o1() + o1());
int useOverflow() { return o2(); }

// One that calls a constexpr function defined after it, also through another
// one defined before it or the default argument of another call, or that
// reads a constant defined after it, couldn't be evaluated where it is
// declared, and is checked where it is used.
constexpr int lateValue();
constexpr int lateRead(int Value = lateValue() + 1) { return Value; } // expected-warning {{overflow in expression; result is -2'147'483'648 with type 'int'}}
constexpr int lateThrough() { return lateValue(); }
constexpr int lateReadThrough(int Value = lateThrough() + 2) { return Value; } // expected-warning {{overflow in expression; result is -2'147'483'647 with type 'int'}}
constexpr int lateValue() { return __INT_MAX__; }
int useLate() { return lateRead() + 1; }
int useLateThrough() { return lateReadThrough() + 1; }
constexpr int lateNestedValue();
constexpr int lateNestedFirst(int Value = lateNestedValue() + 1) { return Value; } // expected-warning {{overflow in expression; result is -2'147'483'648 with type 'int'}}
constexpr int lateNestedSecond(int Value = lateNestedFirst()) { return Value; }
constexpr int lateNestedValue() { return __INT_MAX__; }
int useLateNested() { return lateNestedSecond() + 1; }
extern const int lateConstant;
constexpr int lateConstantRead(int Value = __INT_MAX__ + lateConstant) { return Value; } // expected-warning {{overflow in expression; result is -2'147'483'648 with type 'int'}}
const int lateConstant = 1;
int useLateConstant() { return lateConstantRead() + 1; }

// And the signatures of lambdas nested in a default argument, which the call
// operator, the conversion to a pointer to a function and the static invoker
// of each have, are scanned once where it is used.
int l(int = []() -> decltype([]() -> decltype([]() -> decltype([]() -> decltype([]() -> decltype([]() -> decltype([]() -> decltype([]() -> decltype([]() -> decltype([]() -> decltype([]() -> decltype([]() -> decltype([]() -> decltype([]() -> decltype([]() -> decltype([]() -> decltype([]() -> decltype([]() -> decltype([]() -> decltype([]() -> decltype([]() -> decltype([]() -> decltype([]() -> decltype([]() -> decltype([]() -> decltype([]() -> decltype([]() -> decltype([]() -> decltype([]() -> decltype([]() -> decltype([]() -> decltype([]() -> decltype([]() -> decltype([]() -> decltype([]() -> decltype([]() -> decltype([]() -> decltype([]() -> decltype([]() -> decltype([]() -> decltype(7) { return 0; }()) { return 0; }()) { return 0; }()) { return 0; }()) { return 0; }()) { return 0; }()) { return 0; }()) { return 0; }()) { return 0; }()) { return 0; }()) { return 0; }()) { return 0; }()) { return 0; }()) { return 0; }()) { return 0; }()) { return 0; }()) { return 0; }()) { return 0; }()) { return 0; }()) { return 0; }()) { return 0; }()) { return 0; }()) { return 0; }()) { return 0; }()) { return 0; }()) { return 0; }()) { return 0; }()) { return 0; }()) { return 0; }()) { return 0; }()) { return 0; }()) { return 0; }()) { return 0; }()) { return 0; }()) { return 0; }()) { return 0; }()) { return 0; }()) { return 0; }()) { return 0; }()) { return 0; }());
int useLambdas() { return l(); }

// So are the default arguments rewritten for the immediate invocation at the
// start of a chain, which each use in the rewrite of an outer one shares.
consteval int Id(int x) { return x; }
int c0(int = Id(1));
int c1(int = g(c0(), c0()));
int c2(int = g(c1(), c1()));
int c3(int = g(c2(), c2()));
int c4(int = g(c3(), c3()));
int c5(int = g(c4(), c4()));
int c6(int = g(c5(), c5()));
int c7(int = g(c6(), c6()));
int c8(int = g(c7(), c7()));
int c9(int = g(c8(), c8()));
int c10(int = g(c9(), c9()));
int c11(int = g(c10(), c10()));
int c12(int = g(c11(), c11()));
int c13(int = g(c12(), c12()));
int c14(int = g(c13(), c13()));
int c15(int = g(c14(), c14()));
int c16(int = g(c15(), c15()));
int c17(int = g(c16(), c16()));
int c18(int = g(c17(), c17()));
int c19(int = g(c18(), c18()));
int c20(int = g(c19(), c19()));
int c21(int = g(c20(), c20()));
int c22(int = g(c21(), c21()));
int c23(int = g(c22(), c22()));
int c24(int = g(c23(), c23()));
int c25(int = g(c24(), c24()));
int c26(int = g(c25(), c25()));
int c27(int = g(c26(), c26()));
int c28(int = g(c27(), c27()));
int c29(int = g(c28(), c28()));
int c30(int = g(c29(), c29()));
int c31(int = g(c30(), c30()));
int c32(int = g(c31(), c31()));
int c33(int = g(c32(), c32()));
int c34(int = g(c33(), c33()));
int c35(int = g(c34(), c34()));
int c36(int = g(c35(), c35()));
int c37(int = g(c36(), c36()));
int c38(int = g(c37(), c37()));
int c39(int = g(c38(), c38()));
int c40(int = g(c39(), c39()));
int useImmediate() { return c40(); }

// And so are the source locations of a use, which the rewrite of each is for.
constexpr int line0(int Line = __builtin_LINE()) { return Line; }
constexpr int line1(int Line = line0() + line0()) { return Line; }
constexpr int line2(int Line = line1() + line1()) { return Line; }
static_assert(line2() == 4 * __LINE__);

// And nested initializer lists in a default argument, whose syntactic and
// semantic forms share their initializers, are scanned once where it is used,
// also for an immediate invocation in them.
struct S { int x; };
int il(int = S{.x = S{.x = S{.x = S{.x = S{.x = S{.x = S{.x = S{.x = S{.x = S{.x = S{.x = S{.x = S{.x = S{.x = S{.x = S{.x = S{.x = S{.x = S{.x = S{.x = S{.x = S{.x = S{.x = S{.x = S{.x = S{.x = S{.x = S{.x = S{.x = S{.x = S{.x = S{.x = S{.x = S{.x = S{.x = S{.x = S{.x = S{.x = S{.x = S{.x = 0}.x}.x}.x}.x}.x}.x}.x}.x}.x}.x}.x}.x}.x}.x}.x}.x}.x}.x}.x}.x}.x}.x}.x}.x}.x}.x}.x}.x}.x}.x}.x}.x}.x}.x}.x}.x}.x}.x}.x}.x);
int useLists() { return il(); }
int ilFail(int = S{.x = S{.x = Fail(0)}.x}.x); // expected-error {{call to consteval function 'Fail' is not a constant expression}}
int useListsFail() { return ilFail(); }
// A default argument with a source location is rewritten for where it is
// used, and can overflow there although it doesn't where it is declared.
int overflowAt(int = (__INT_MAX__ - 1000) + (int)__builtin_LINE()); // expected-warning {{overflow in expression; result is -2'147'482'649 with type 'int'}}
#line 2000
int useOverflowAt() { return overflowAt() + 1; }
#line 184
// And the rewrites the uses share are checked once.
int ln0(int = (int)__builtin_LINE());
int ln1(int = ln0() + ln0());
int ln2(int = ln1() + ln1());
int ln3(int = ln2() + ln2());
int ln4(int = ln3() + ln3());
int ln5(int = ln4() + ln4());
int ln6(int = ln5() + ln5());
int ln7(int = ln6() + ln6());
int ln8(int = ln7() + ln7());
int ln9(int = ln8() + ln8());
int ln10(int = ln9() + ln9());
int ln11(int = ln10() + ln10());
int ln12(int = ln11() + ln11());
int ln13(int = ln12() + ln12());
int ln14(int = ln13() + ln13());
int ln15(int = ln14() + ln14());
int ln16(int = ln15() + ln15());
int ln17(int = ln16() + ln16());
int ln18(int = ln17() + ln17());
int ln19(int = ln18() + ln18());
int ln20(int = ln19() + ln19());
int ln21(int = ln20() + ln20());
int ln22(int = ln21() + ln21());
int ln23(int = ln22() + ln22());
int ln24(int = ln23() + ln23());
int ln25(int = ln24() + ln24());
int ln26(int = ln25() + ln25());
int ln27(int = ln26() + ln26());
int ln28(int = ln27() + ln27());
int ln29(int = ln28() + ln28());
int ln30(int = ln29() + ln29());
int ln31(int = ln30() + ln30());
int ln32(int = ln31() + ln31());
int ln33(int = ln32() + ln32());
int ln34(int = ln33() + ln33());
int ln35(int = ln34() + ln34());
int ln36(int = ln35() + ln35());
int ln37(int = ln36() + ln36());
int ln38(int = ln37() + ln37());
int ln39(int = ln38() + ln38());
int ln40(int = ln39() + ln39());
int useLines() { return ln40() + 1; }

// An initializer that a later designator overrides is only in the syntactic
// form, and an immediate invocation in it is still checked. The rewrite of the
// default argument for its use warns about the override again.
int ilOverridden(int = S{.x = Fail(0), .x = 1}.x); // expected-error {{call to consteval function 'Fail' is not a constant expression}} expected-warning 2 {{initializer overrides prior initialization of this subobject}}
int useListsOverridden() { return ilOverridden(); }

// And nested co_await in a coroutine lambda in a default argument, whose
// operands the expressions that make their awaiters share, are scanned once.
#include "std-coroutine.h"
struct Task {
  struct promise_type {
    Task get_return_object();
    std::suspend_never initial_suspend();
    std::suspend_never final_suspend() noexcept;
    void return_value(int);
    void unhandled_exception();
  };
};
struct Awaiter {
  bool await_ready();
  void await_suspend(std::coroutine_handle<>);
  int await_resume();
};
Awaiter work(int);
#define A(x) co_await work(x)
#define A4(x) A(A(A(A(x))))
#define A16(x) A4(A4(A4(A4(x))))
void coro(Task = []() -> Task { co_return A16(A16(0)); }());
void useCoro() { coro(); }
// Also when the check for overflow evaluates the call.
int coroValue(Task = []() -> Task { co_return A16(A16(0)); }());
int useCoroValue() { return coroValue() + 1; }

// A default member initializer can depend on the object it initializes, so it
// is checked for each, whichever comes first.
struct MemberOverflow {
  int x;
  int y = __INT_MAX__ + x; // expected-warning 2{{overflow in expression; result is -2'147'483'648 with type 'int'}}
  constexpr MemberOverflow(int a) : x(a) {}
};
int useMemberOverflow() { return MemberOverflow(0).y + MemberOverflow(1).y; }
int useMemberOverflowReversed() { return MemberOverflow(1).y + MemberOverflow(0).y; }

// So are the default member initializers of a chain of classes whose member
// is initialized with two of the one before, rewritten for the immediate
// invocation in the first, which the uses inside the rewrite share.
extern bool dmiChoose;
struct D0 { int m = Id(1); };
struct D1 { D0 m = dmiChoose ? D0{} : D0{}; };
struct D2 { D1 m = dmiChoose ? D1{} : D1{}; };
struct D3 { D2 m = dmiChoose ? D2{} : D2{}; };
struct D4 { D3 m = dmiChoose ? D3{} : D3{}; };
struct D5 { D4 m = dmiChoose ? D4{} : D4{}; };
struct D6 { D5 m = dmiChoose ? D5{} : D5{}; };
struct D7 { D6 m = dmiChoose ? D6{} : D6{}; };
struct D8 { D7 m = dmiChoose ? D7{} : D7{}; };
struct D9 { D8 m = dmiChoose ? D8{} : D8{}; };
struct D10 { D9 m = dmiChoose ? D9{} : D9{}; };
struct D11 { D10 m = dmiChoose ? D10{} : D10{}; };
struct D12 { D11 m = dmiChoose ? D11{} : D11{}; };
struct D13 { D12 m = dmiChoose ? D12{} : D12{}; };
struct D14 { D13 m = dmiChoose ? D13{} : D13{}; };
struct D15 { D14 m = dmiChoose ? D14{} : D14{}; };
struct D16 { D15 m = dmiChoose ? D15{} : D15{}; };
struct D17 { D16 m = dmiChoose ? D16{} : D16{}; };
struct D18 { D17 m = dmiChoose ? D17{} : D17{}; };
struct D19 { D18 m = dmiChoose ? D18{} : D18{}; };
struct D20 { D19 m = dmiChoose ? D19{} : D19{}; };
struct D21 { D20 m = dmiChoose ? D20{} : D20{}; };
struct D22 { D21 m = dmiChoose ? D21{} : D21{}; };
struct D23 { D22 m = dmiChoose ? D22{} : D22{}; };
struct D24 { D23 m = dmiChoose ? D23{} : D23{}; };
struct D25 { D24 m = dmiChoose ? D24{} : D24{}; };
struct D26 { D25 m = dmiChoose ? D25{} : D25{}; };
struct D27 { D26 m = dmiChoose ? D26{} : D26{}; };
struct D28 { D27 m = dmiChoose ? D27{} : D27{}; };
struct D29 { D28 m = dmiChoose ? D28{} : D28{}; };
struct D30 { D29 m = dmiChoose ? D29{} : D29{}; };
struct D31 { D30 m = dmiChoose ? D30{} : D30{}; };
struct D32 { D31 m = dmiChoose ? D31{} : D31{}; };
struct D33 { D32 m = dmiChoose ? D32{} : D32{}; };
struct D34 { D33 m = dmiChoose ? D33{} : D33{}; };
struct D35 { D34 m = dmiChoose ? D34{} : D34{}; };
struct D36 { D35 m = dmiChoose ? D35{} : D35{}; };
struct D37 { D36 m = dmiChoose ? D36{} : D36{}; };
struct D38 { D37 m = dmiChoose ? D37{} : D37{}; };
struct D39 { D38 m = dmiChoose ? D38{} : D38{}; };
struct D40 { D39 m = dmiChoose ? D39{} : D39{}; };
void dmiUse() { D40 d; }

// A rewrite that materializes a temporary isn't shared: each use has its own,
// whose lifetime the aggregate it initializes extends.
struct DmiTemporary { int const &m = Id(42); };
struct DmiTemporaryPair { DmiTemporary first; DmiTemporary second; };
constexpr DmiTemporaryPair dmiTemporaryPair{};
static_assert(&dmiTemporaryPair.first.m != &dmiTemporaryPair.second.m);
static_assert(dmiTemporaryPair.first.m == 42 && dmiTemporaryPair.second.m == 42);

// But one whose value is discarded, `(void)(int const &)42`, and the left
// operand of a comma, last until the end of the full expression, so the
// rewrite is shared and the uses of this chain are checked in linear time.
template <int N> struct DmiDiscarded {
  DmiDiscarded<N - 1> m =
      dmiChoose ? DmiDiscarded<N - 1>{} : DmiDiscarded<N - 1>{};
};
template <> struct DmiDiscarded<0> {
  int m = ((void)(int const &)42, __builtin_LINE());
};
void dmiDiscardedUse() { DmiDiscarded<40> d; }

// So does one whose value is read, `S{}.x`, which the rewrite of a default
// member initializer for its temporaries, without an immediate invocation or
// a source location, shares too.
template <int N> struct DmiRead {
  int x = dmiChoose ? DmiRead<N - 1>{}.x : DmiRead<N - 1>{}.x;
};
template <> struct DmiRead<0> { int x = 0; };
void dmiReadUse() { DmiRead<40> d; }

// One that isn't evaluated, `sizeof(probe(1))`, makes no temporary, so the
// rewrite is shared and the uses of this chain are checked in linear time.
int probe(int const &);
int u0(int = Id(sizeof(probe(1))));
int u1(int = u0() + u0());
int u2(int = u1() + u1());
int u3(int = u2() + u2());
int u4(int = u3() + u3());
int u5(int = u4() + u4());
int u6(int = u5() + u5());
int u7(int = u6() + u6());
int u8(int = u7() + u7());
int u9(int = u8() + u8());
int u10(int = u9() + u9());
int u11(int = u10() + u10());
int u12(int = u11() + u11());
int u13(int = u12() + u12());
int u14(int = u13() + u13());
int u15(int = u14() + u14());
int u16(int = u15() + u15());
int u17(int = u16() + u16());
int u18(int = u17() + u17());
int u19(int = u18() + u18());
int u20(int = u19() + u19());
int u21(int = u20() + u20());
int u22(int = u21() + u21());
int u23(int = u22() + u22());
int u24(int = u23() + u23());
int u25(int = u24() + u24());
int u26(int = u25() + u25());
int u27(int = u26() + u26());
int u28(int = u27() + u27());
int u29(int = u28() + u28());
int u30(int = u29() + u29());
int u31(int = u30() + u30());
int u32(int = u31() + u31());
int u33(int = u32() + u32());
int u34(int = u33() + u33());
int u35(int = u34() + u34());
int u36(int = u35() + u35());
int u37(int = u36() + u36());
int u38(int = u37() + u37());
int u39(int = u38() + u38());
int u40(int = u39() + u39());
int useUnevaluated() { return u40(); }

// One whose temporary a reference parameter binds lasts until the end of the
// full expression each time it is evaluated, so its rewrite is shared too.
extern bool tChoose;
int t0(int const & = Id(1));
int t1(int = tChoose ? t0() : t0());
int t2(int = tChoose ? t1() : t1());
int t3(int = tChoose ? t2() : t2());
int t4(int = tChoose ? t3() : t3());
int t5(int = tChoose ? t4() : t4());
int t6(int = tChoose ? t5() : t5());
int t7(int = tChoose ? t6() : t6());
int t8(int = tChoose ? t7() : t7());
int t9(int = tChoose ? t8() : t8());
int t10(int = tChoose ? t9() : t9());
int t11(int = tChoose ? t10() : t10());
int t12(int = tChoose ? t11() : t11());
int t13(int = tChoose ? t12() : t12());
int t14(int = tChoose ? t13() : t13());
int t15(int = tChoose ? t14() : t14());
int t16(int = tChoose ? t15() : t15());
int t17(int = tChoose ? t16() : t16());
int t18(int = tChoose ? t17() : t17());
int t19(int = tChoose ? t18() : t18());
int t20(int = tChoose ? t19() : t19());
int t21(int = tChoose ? t20() : t20());
int t22(int = tChoose ? t21() : t21());
int t23(int = tChoose ? t22() : t22());
int t24(int = tChoose ? t23() : t23());
int t25(int = tChoose ? t24() : t24());
int t26(int = tChoose ? t25() : t25());
int t27(int = tChoose ? t26() : t26());
int t28(int = tChoose ? t27() : t27());
int t29(int = tChoose ? t28() : t28());
int t30(int = tChoose ? t29() : t29());
int t31(int = tChoose ? t30() : t30());
int t32(int = tChoose ? t31() : t31());
int t33(int = tChoose ? t32() : t32());
int t34(int = tChoose ? t33() : t33());
int t35(int = tChoose ? t34() : t34());
int t36(int = tChoose ? t35() : t35());
int t37(int = tChoose ? t36() : t36());
int t38(int = tChoose ? t37() : t37());
int t39(int = tChoose ? t38() : t38());
int t40(int = tChoose ? t39() : t39());
int useTemporary() { return t40(); }

// Not one a call is given, also as its object, `[] { ... }()`, which lasts
// until the end of the full expression: the rewrite is shared.
struct E0 { int m = [] { return __builtin_LINE(); }(); };
struct E1 { E0 m = dmiChoose ? E0{} : E0{}; };
struct E2 { E1 m = dmiChoose ? E1{} : E1{}; };
struct E3 { E2 m = dmiChoose ? E2{} : E2{}; };
struct E4 { E3 m = dmiChoose ? E3{} : E3{}; };
struct E5 { E4 m = dmiChoose ? E4{} : E4{}; };
struct E6 { E5 m = dmiChoose ? E5{} : E5{}; };
struct E7 { E6 m = dmiChoose ? E6{} : E6{}; };
struct E8 { E7 m = dmiChoose ? E7{} : E7{}; };
struct E9 { E8 m = dmiChoose ? E8{} : E8{}; };
struct E10 { E9 m = dmiChoose ? E9{} : E9{}; };
struct E11 { E10 m = dmiChoose ? E10{} : E10{}; };
struct E12 { E11 m = dmiChoose ? E11{} : E11{}; };
struct E13 { E12 m = dmiChoose ? E12{} : E12{}; };
struct E14 { E13 m = dmiChoose ? E13{} : E13{}; };
struct E15 { E14 m = dmiChoose ? E14{} : E14{}; };
struct E16 { E15 m = dmiChoose ? E15{} : E15{}; };
struct E17 { E16 m = dmiChoose ? E16{} : E16{}; };
struct E18 { E17 m = dmiChoose ? E17{} : E17{}; };
struct E19 { E18 m = dmiChoose ? E18{} : E18{}; };
struct E20 { E19 m = dmiChoose ? E19{} : E19{}; };
struct E21 { E20 m = dmiChoose ? E20{} : E20{}; };
struct E22 { E21 m = dmiChoose ? E21{} : E21{}; };
struct E23 { E22 m = dmiChoose ? E22{} : E22{}; };
struct E24 { E23 m = dmiChoose ? E23{} : E23{}; };
struct E25 { E24 m = dmiChoose ? E24{} : E24{}; };
struct E26 { E25 m = dmiChoose ? E25{} : E25{}; };
struct E27 { E26 m = dmiChoose ? E26{} : E26{}; };
struct E28 { E27 m = dmiChoose ? E27{} : E27{}; };
struct E29 { E28 m = dmiChoose ? E28{} : E28{}; };
struct E30 { E29 m = dmiChoose ? E29{} : E29{}; };
struct E31 { E30 m = dmiChoose ? E30{} : E30{}; };
struct E32 { E31 m = dmiChoose ? E31{} : E31{}; };
struct E33 { E32 m = dmiChoose ? E32{} : E32{}; };
struct E34 { E33 m = dmiChoose ? E33{} : E33{}; };
struct E35 { E34 m = dmiChoose ? E34{} : E34{}; };
struct E36 { E35 m = dmiChoose ? E35{} : E35{}; };
struct E37 { E36 m = dmiChoose ? E36{} : E36{}; };
struct E38 { E37 m = dmiChoose ? E37{} : E37{}; };
struct E39 { E38 m = dmiChoose ? E38{} : E38{}; };
struct E40 { E39 m = dmiChoose ? E39{} : E39{}; };
void dmiCallUse() { E40 e; }

// Whether the use of the last of a chain whose default argument adds two calls
// of the one before can throw is told once for each default argument, also
// the exception specification of an implicit constructor, which tells it of
// each default member initializer of a chain like the one above.
int n0(int = 0) noexcept;
int n1(int = n0() + n0()) noexcept;
int n2(int = n1() + n1()) noexcept;
int n3(int = n2() + n2()) noexcept;
int n4(int = n3() + n3()) noexcept;
int n5(int = n4() + n4()) noexcept;
int n6(int = n5() + n5()) noexcept;
int n7(int = n6() + n6()) noexcept;
int n8(int = n7() + n7()) noexcept;
int n9(int = n8() + n8()) noexcept;
int n10(int = n9() + n9()) noexcept;
int n11(int = n10() + n10()) noexcept;
int n12(int = n11() + n11()) noexcept;
int n13(int = n12() + n12()) noexcept;
int n14(int = n13() + n13()) noexcept;
int n15(int = n14() + n14()) noexcept;
int n16(int = n15() + n15()) noexcept;
int n17(int = n16() + n16()) noexcept;
int n18(int = n17() + n17()) noexcept;
int n19(int = n18() + n18()) noexcept;
int n20(int = n19() + n19()) noexcept;
int n21(int = n20() + n20()) noexcept;
int n22(int = n21() + n21()) noexcept;
int n23(int = n22() + n22()) noexcept;
int n24(int = n23() + n23()) noexcept;
int n25(int = n24() + n24()) noexcept;
int n26(int = n25() + n25()) noexcept;
int n27(int = n26() + n26()) noexcept;
int n28(int = n27() + n27()) noexcept;
int n29(int = n28() + n28()) noexcept;
int n30(int = n29() + n29()) noexcept;
int n31(int = n30() + n30()) noexcept;
int n32(int = n31() + n31()) noexcept;
int n33(int = n32() + n32()) noexcept;
int n34(int = n33() + n33()) noexcept;
int n35(int = n34() + n34()) noexcept;
int n36(int = n35() + n35()) noexcept;
int n37(int = n36() + n36()) noexcept;
int n38(int = n37() + n37()) noexcept;
int n39(int = n38() + n38()) noexcept;
int n40(int = n39() + n39()) noexcept;
static_assert(noexcept(n40()));
// Also in an instantiation, which doesn't check it for side effects.
template <int I> void noexceptUse() { static_assert(noexcept(n40() + I)); }
template void noexceptUse<0>();
struct N0 { int m = 0; };
struct N1 { N0 m = dmiChoose ? N0{} : N0{}; };
struct N2 { N1 m = dmiChoose ? N1{} : N1{}; };
struct N3 { N2 m = dmiChoose ? N2{} : N2{}; };
struct N4 { N3 m = dmiChoose ? N3{} : N3{}; };
struct N5 { N4 m = dmiChoose ? N4{} : N4{}; };
struct N6 { N5 m = dmiChoose ? N5{} : N5{}; };
struct N7 { N6 m = dmiChoose ? N6{} : N6{}; };
struct N8 { N7 m = dmiChoose ? N7{} : N7{}; };
struct N9 { N8 m = dmiChoose ? N8{} : N8{}; };
struct N10 { N9 m = dmiChoose ? N9{} : N9{}; };
struct N11 { N10 m = dmiChoose ? N10{} : N10{}; };
struct N12 { N11 m = dmiChoose ? N11{} : N11{}; };
struct N13 { N12 m = dmiChoose ? N12{} : N12{}; };
struct N14 { N13 m = dmiChoose ? N13{} : N13{}; };
struct N15 { N14 m = dmiChoose ? N14{} : N14{}; };
struct N16 { N15 m = dmiChoose ? N15{} : N15{}; };
struct N17 { N16 m = dmiChoose ? N16{} : N16{}; };
struct N18 { N17 m = dmiChoose ? N17{} : N17{}; };
struct N19 { N18 m = dmiChoose ? N18{} : N18{}; };
struct N20 { N19 m = dmiChoose ? N19{} : N19{}; };
struct N21 { N20 m = dmiChoose ? N20{} : N20{}; };
struct N22 { N21 m = dmiChoose ? N21{} : N21{}; };
struct N23 { N22 m = dmiChoose ? N22{} : N22{}; };
struct N24 { N23 m = dmiChoose ? N23{} : N23{}; };
struct N25 { N24 m = dmiChoose ? N24{} : N24{}; };
struct N26 { N25 m = dmiChoose ? N25{} : N25{}; };
struct N27 { N26 m = dmiChoose ? N26{} : N26{}; };
struct N28 { N27 m = dmiChoose ? N27{} : N27{}; };
struct N29 { N28 m = dmiChoose ? N28{} : N28{}; };
struct N30 { N29 m = dmiChoose ? N29{} : N29{}; };
struct N31 { N30 m = dmiChoose ? N30{} : N30{}; };
struct N32 { N31 m = dmiChoose ? N31{} : N31{}; };
struct N33 { N32 m = dmiChoose ? N32{} : N32{}; };
struct N34 { N33 m = dmiChoose ? N33{} : N33{}; };
struct N35 { N34 m = dmiChoose ? N34{} : N34{}; };
struct N36 { N35 m = dmiChoose ? N35{} : N35{}; };
struct N37 { N36 m = dmiChoose ? N36{} : N36{}; };
struct N38 { N37 m = dmiChoose ? N37{} : N37{}; };
struct N39 { N38 m = dmiChoose ? N38{} : N38{}; };
struct N40 { N39 m = dmiChoose ? N39{} : N39{}; };
static_assert(noexcept(N40()));
void dmiNoexceptUse() { N40 n; }

// Also where a temporary of the immediate invocation is bound to a reference
// member in parenthesized aggregate initialization, `R(Id(0))`, which doesn't
// extend its lifetime: it lasts until the end of the full expression, like one
// a call is given, so the uses share the rewrite.
struct ParenRef { int const &r; };
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdangling"
struct P0 { ParenRef r = ParenRef(Id(0)); };
#pragma clang diagnostic pop
#define PAREN_LEVEL(Level, Previous) struct P##Level { P##Previous m = dmiChoose ? P##Previous{} : P##Previous{}; };
PAREN_LEVEL(1, 0) PAREN_LEVEL(2, 1) PAREN_LEVEL(3, 2) PAREN_LEVEL(4, 3)
PAREN_LEVEL(5, 4) PAREN_LEVEL(6, 5) PAREN_LEVEL(7, 6) PAREN_LEVEL(8, 7)
PAREN_LEVEL(9, 8) PAREN_LEVEL(10, 9) PAREN_LEVEL(11, 10) PAREN_LEVEL(12, 11)
PAREN_LEVEL(13, 12) PAREN_LEVEL(14, 13) PAREN_LEVEL(15, 14) PAREN_LEVEL(16, 15)
PAREN_LEVEL(17, 16) PAREN_LEVEL(18, 17) PAREN_LEVEL(19, 18) PAREN_LEVEL(20, 19)
PAREN_LEVEL(21, 20) PAREN_LEVEL(22, 21) PAREN_LEVEL(23, 22) PAREN_LEVEL(24, 23)
PAREN_LEVEL(25, 24) PAREN_LEVEL(26, 25) PAREN_LEVEL(27, 26) PAREN_LEVEL(28, 27)
PAREN_LEVEL(29, 28) PAREN_LEVEL(30, 29) PAREN_LEVEL(31, 30) PAREN_LEVEL(32, 31)
PAREN_LEVEL(33, 32) PAREN_LEVEL(34, 33) PAREN_LEVEL(35, 34) PAREN_LEVEL(36, 35)
PAREN_LEVEL(37, 36) PAREN_LEVEL(38, 37) PAREN_LEVEL(39, 38) PAREN_LEVEL(40, 39)
void parenRefUse() { P40 p{}; (void)p; }
