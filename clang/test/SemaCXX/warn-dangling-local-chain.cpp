// RUN: %clang_cc1 -fsyntax-only -verify -verify-ignore-unexpected=note %s

// A chain of references, each bound to a conditional of the one before, is
// checked in time linear in its length, not exponential, and still finds the
// local the first one refers to.
int &chain(bool b) {
  int x = 0;
  int &r0 = x;
  int &r1 = b ? r0 : r0;
  int &r2 = b ? r1 : r1;
  int &r3 = b ? r2 : r2;
  int &r4 = b ? r3 : r3;
  int &r5 = b ? r4 : r4;
  int &r6 = b ? r5 : r5;
  int &r7 = b ? r6 : r6;
  int &r8 = b ? r7 : r7;
  int &r9 = b ? r8 : r8;
  int &r10 = b ? r9 : r9;
  int &r11 = b ? r10 : r10;
  int &r12 = b ? r11 : r11;
  int &r13 = b ? r12 : r12;
  int &r14 = b ? r13 : r13;
  int &r15 = b ? r14 : r14;
  int &r16 = b ? r15 : r15;
  int &r17 = b ? r16 : r16;
  int &r18 = b ? r17 : r17;
  int &r19 = b ? r18 : r18;
  int &r20 = b ? r19 : r19;
  int &r21 = b ? r20 : r20;
  int &r22 = b ? r21 : r21;
  int &r23 = b ? r22 : r22;
  int &r24 = b ? r23 : r23;
  int &r25 = b ? r24 : r24;
  int &r26 = b ? r25 : r25;
  int &r27 = b ? r26 : r26;
  int &r28 = b ? r27 : r27;
  int &r29 = b ? r28 : r28;
  int &r30 = b ? r29 : r29;
  int &r31 = b ? r30 : r30;
  int &r32 = b ? r31 : r31;
  int &r33 = b ? r32 : r32;
  int &r34 = b ? r33 : r33;
  int &r35 = b ? r34 : r34;
  int &r36 = b ? r35 : r35;
  int &r37 = b ? r36 : r36;
  int &r38 = b ? r37 : r37;
  int &r39 = b ? r38 : r38;
  int &r40 = b ? r39 : r39;
  return r40; // expected-warning {{reference to stack memory associated with local variable 'x' returned}}
}

// Also of each of the two before, which reach them along many paths; the
// two that refer to the local each tell of it.
int &chain2(bool b) {
  int x = 0;
  int &r0 = x;
  int &r1 = x;
  int &r2 = b ? r1 : r0;
  int &r3 = b ? r2 : r1;
  int &r4 = b ? r3 : r2;
  int &r5 = b ? r4 : r3;
  int &r6 = b ? r5 : r4;
  int &r7 = b ? r6 : r5;
  int &r8 = b ? r7 : r6;
  int &r9 = b ? r8 : r7;
  int &r10 = b ? r9 : r8;
  int &r11 = b ? r10 : r9;
  int &r12 = b ? r11 : r10;
  int &r13 = b ? r12 : r11;
  int &r14 = b ? r13 : r12;
  int &r15 = b ? r14 : r13;
  int &r16 = b ? r15 : r14;
  int &r17 = b ? r16 : r15;
  int &r18 = b ? r17 : r16;
  int &r19 = b ? r18 : r17;
  int &r20 = b ? r19 : r18;
  int &r21 = b ? r20 : r19;
  int &r22 = b ? r21 : r20;
  int &r23 = b ? r22 : r21;
  int &r24 = b ? r23 : r22;
  int &r25 = b ? r24 : r23;
  int &r26 = b ? r25 : r24;
  int &r27 = b ? r26 : r25;
  int &r28 = b ? r27 : r26;
  int &r29 = b ? r28 : r27;
  int &r30 = b ? r29 : r28;
  int &r31 = b ? r30 : r29;
  int &r32 = b ? r31 : r30;
  int &r33 = b ? r32 : r31;
  int &r34 = b ? r33 : r32;
  int &r35 = b ? r34 : r33;
  int &r36 = b ? r35 : r34;
  int &r37 = b ? r36 : r35;
  int &r38 = b ? r37 : r36;
  int &r39 = b ? r38 : r37;
  int &r40 = b ? r39 : r38;
  int &r41 = b ? r40 : r39;
  return r41; // expected-warning 2 {{reference to stack memory associated with local variable 'x' returned}}
}

// A chain where each reference is bound to one of two lifetimebound calls on the
// one before is linear too.
int &first(int &v [[clang::lifetimebound]]);
int &second(int &v [[clang::lifetimebound]]);
int &calls(bool b) {
  int x = 0;
  int &r0 = x;
  int &r1 = b ? first(r0) : second(r0);
  int &r2 = b ? first(r1) : second(r1);
  int &r3 = b ? first(r2) : second(r2);
  int &r4 = b ? first(r3) : second(r3);
  int &r5 = b ? first(r4) : second(r4);
  int &r6 = b ? first(r5) : second(r5);
  int &r7 = b ? first(r6) : second(r6);
  int &r8 = b ? first(r7) : second(r7);
  int &r9 = b ? first(r8) : second(r8);
  int &r10 = b ? first(r9) : second(r9);
  int &r11 = b ? first(r10) : second(r10);
  int &r12 = b ? first(r11) : second(r11);
  int &r13 = b ? first(r12) : second(r12);
  int &r14 = b ? first(r13) : second(r13);
  int &r15 = b ? first(r14) : second(r14);
  int &r16 = b ? first(r15) : second(r15);
  int &r17 = b ? first(r16) : second(r16);
  int &r18 = b ? first(r17) : second(r17);
  int &r19 = b ? first(r18) : second(r18);
  int &r20 = b ? first(r19) : second(r19);
  int &r21 = b ? first(r20) : second(r20);
  int &r22 = b ? first(r21) : second(r21);
  int &r23 = b ? first(r22) : second(r22);
  int &r24 = b ? first(r23) : second(r23);
  int &r25 = b ? first(r24) : second(r24);
  int &r26 = b ? first(r25) : second(r25);
  int &r27 = b ? first(r26) : second(r26);
  int &r28 = b ? first(r27) : second(r27);
  int &r29 = b ? first(r28) : second(r28);
  int &r30 = b ? first(r29) : second(r29);
  int &r31 = b ? first(r30) : second(r30);
  int &r32 = b ? first(r31) : second(r31);
  int &r33 = b ? first(r32) : second(r32);
  int &r34 = b ? first(r33) : second(r33);
  int &r35 = b ? first(r34) : second(r34);
  int &r36 = b ? first(r35) : second(r35);
  int &r37 = b ? first(r36) : second(r36);
  int &r38 = b ? first(r37) : second(r37);
  int &r39 = b ? first(r38) : second(r38);
  int &r40 = b ? first(r39) : second(r39);
  return r40; // expected-warning {{reference to stack memory associated with local variable 'x' returned}}
}

// So are the default arguments of a chain of functions that each uses the
// one before twice through lifetimebound parameters, which their uses share,
// and a temporary in the first is still found.
extern const int dGlobal;
extern bool dB;
const int &pick(const int &a [[clang::lifetimebound]], const int &b [[clang::lifetimebound]]);
const int &d0(const int &v [[clang::lifetimebound]] = dGlobal);
const int &d1(const int &v [[clang::lifetimebound]] = pick(d0(), dB ? d0() : d0()));
const int &d2(const int &v [[clang::lifetimebound]] = pick(d1(), dB ? d1() : d1()));
const int &d3(const int &v [[clang::lifetimebound]] = pick(d2(), dB ? d2() : d2()));
const int &d4(const int &v [[clang::lifetimebound]] = pick(d3(), dB ? d3() : d3()));
const int &d5(const int &v [[clang::lifetimebound]] = pick(d4(), dB ? d4() : d4()));
const int &d6(const int &v [[clang::lifetimebound]] = pick(d5(), dB ? d5() : d5()));
const int &d7(const int &v [[clang::lifetimebound]] = pick(d6(), dB ? d6() : d6()));
const int &d8(const int &v [[clang::lifetimebound]] = pick(d7(), dB ? d7() : d7()));
const int &d9(const int &v [[clang::lifetimebound]] = pick(d8(), dB ? d8() : d8()));
const int &d10(const int &v [[clang::lifetimebound]] = pick(d9(), dB ? d9() : d9()));
const int &d11(const int &v [[clang::lifetimebound]] = pick(d10(), dB ? d10() : d10()));
const int &d12(const int &v [[clang::lifetimebound]] = pick(d11(), dB ? d11() : d11()));
const int &d13(const int &v [[clang::lifetimebound]] = pick(d12(), dB ? d12() : d12()));
const int &d14(const int &v [[clang::lifetimebound]] = pick(d13(), dB ? d13() : d13()));
const int &d15(const int &v [[clang::lifetimebound]] = pick(d14(), dB ? d14() : d14()));
const int &d16(const int &v [[clang::lifetimebound]] = pick(d15(), dB ? d15() : d15()));
const int &d17(const int &v [[clang::lifetimebound]] = pick(d16(), dB ? d16() : d16()));
const int &d18(const int &v [[clang::lifetimebound]] = pick(d17(), dB ? d17() : d17()));
const int &d19(const int &v [[clang::lifetimebound]] = pick(d18(), dB ? d18() : d18()));
const int &d20(const int &v [[clang::lifetimebound]] = pick(d19(), dB ? d19() : d19()));
const int &d21(const int &v [[clang::lifetimebound]] = pick(d20(), dB ? d20() : d20()));
const int &d22(const int &v [[clang::lifetimebound]] = pick(d21(), dB ? d21() : d21()));
const int &d23(const int &v [[clang::lifetimebound]] = pick(d22(), dB ? d22() : d22()));
const int &d24(const int &v [[clang::lifetimebound]] = pick(d23(), dB ? d23() : d23()));
const int &d25(const int &v [[clang::lifetimebound]] = pick(d24(), dB ? d24() : d24()));
const int &d26(const int &v [[clang::lifetimebound]] = pick(d25(), dB ? d25() : d25()));
const int &d27(const int &v [[clang::lifetimebound]] = pick(d26(), dB ? d26() : d26()));
const int &d28(const int &v [[clang::lifetimebound]] = pick(d27(), dB ? d27() : d27()));
const int &d29(const int &v [[clang::lifetimebound]] = pick(d28(), dB ? d28() : d28()));
const int &d30(const int &v [[clang::lifetimebound]] = pick(d29(), dB ? d29() : d29()));
const int &d31(const int &v [[clang::lifetimebound]] = pick(d30(), dB ? d30() : d30()));
const int &d32(const int &v [[clang::lifetimebound]] = pick(d31(), dB ? d31() : d31()));
const int &d33(const int &v [[clang::lifetimebound]] = pick(d32(), dB ? d32() : d32()));
const int &d34(const int &v [[clang::lifetimebound]] = pick(d33(), dB ? d33() : d33()));
const int &d35(const int &v [[clang::lifetimebound]] = pick(d34(), dB ? d34() : d34()));
const int &d36(const int &v [[clang::lifetimebound]] = pick(d35(), dB ? d35() : d35()));
const int &d37(const int &v [[clang::lifetimebound]] = pick(d36(), dB ? d36() : d36()));
const int &d38(const int &v [[clang::lifetimebound]] = pick(d37(), dB ? d37() : d37()));
const int &d39(const int &v [[clang::lifetimebound]] = pick(d38(), dB ? d38() : d38()));
const int &d40(const int &v [[clang::lifetimebound]] = pick(d39(), dB ? d39() : d39()));
const int &useDefaults() { return d40(); }

const int &t0(const int &v [[clang::lifetimebound]] = 1);
const int &t1(const int &v [[clang::lifetimebound]] = pick(t0(), t0()));
const int &t2(const int &v [[clang::lifetimebound]] = pick(t1(), t1()));
const int &t3(const int &v [[clang::lifetimebound]] = pick(t2(), t2()));
const int &t4(const int &v [[clang::lifetimebound]] = pick(t3(), t3()));
const int &t5(const int &v [[clang::lifetimebound]] = pick(t4(), t4()));
const int &t6(const int &v [[clang::lifetimebound]] = pick(t5(), t5()));
const int &t7(const int &v [[clang::lifetimebound]] = pick(t6(), t6()));
const int &t8(const int &v [[clang::lifetimebound]] = pick(t7(), t7()));
const int &t9(const int &v [[clang::lifetimebound]] = pick(t8(), t8()));
const int &t10(const int &v [[clang::lifetimebound]] = pick(t9(), t9()));
const int &t11(const int &v [[clang::lifetimebound]] = pick(t10(), t10()));
const int &t12(const int &v [[clang::lifetimebound]] = pick(t11(), t11()));
const int &t13(const int &v [[clang::lifetimebound]] = pick(t12(), t12()));
const int &t14(const int &v [[clang::lifetimebound]] = pick(t13(), t13()));
const int &t15(const int &v [[clang::lifetimebound]] = pick(t14(), t14()));
const int &t16(const int &v [[clang::lifetimebound]] = pick(t15(), t15()));
const int &t17(const int &v [[clang::lifetimebound]] = pick(t16(), t16()));
const int &t18(const int &v [[clang::lifetimebound]] = pick(t17(), t17()));
const int &t19(const int &v [[clang::lifetimebound]] = pick(t18(), t18()));
const int &t20(const int &v [[clang::lifetimebound]] = pick(t19(), t19()));
const int &t21(const int &v [[clang::lifetimebound]] = pick(t20(), t20()));
const int &t22(const int &v [[clang::lifetimebound]] = pick(t21(), t21()));
const int &t23(const int &v [[clang::lifetimebound]] = pick(t22(), t22()));
const int &t24(const int &v [[clang::lifetimebound]] = pick(t23(), t23()));
const int &t25(const int &v [[clang::lifetimebound]] = pick(t24(), t24()));
const int &t26(const int &v [[clang::lifetimebound]] = pick(t25(), t25()));
const int &t27(const int &v [[clang::lifetimebound]] = pick(t26(), t26()));
const int &t28(const int &v [[clang::lifetimebound]] = pick(t27(), t27()));
const int &t29(const int &v [[clang::lifetimebound]] = pick(t28(), t28()));
const int &t30(const int &v [[clang::lifetimebound]] = pick(t29(), t29()));
const int &t31(const int &v [[clang::lifetimebound]] = pick(t30(), t30()));
const int &t32(const int &v [[clang::lifetimebound]] = pick(t31(), t31()));
const int &t33(const int &v [[clang::lifetimebound]] = pick(t32(), t32()));
const int &t34(const int &v [[clang::lifetimebound]] = pick(t33(), t33()));
const int &t35(const int &v [[clang::lifetimebound]] = pick(t34(), t34()));
const int &t36(const int &v [[clang::lifetimebound]] = pick(t35(), t35()));
const int &t37(const int &v [[clang::lifetimebound]] = pick(t36(), t36()));
const int &t38(const int &v [[clang::lifetimebound]] = pick(t37(), t37()));
const int &t39(const int &v [[clang::lifetimebound]] = pick(t38(), t38()));
const int &t40(const int &v [[clang::lifetimebound]] = pick(t39(), t39()));
const int &useTemporary() { return t40(); } // expected-warning {{returning reference to local temporary object}}

// So is a chain of default member initializers, each a conditional of the one
// before, which each initialization that uses one shares.
extern bool b;
template <int N> struct DefaultInitChain {
  DefaultInitChain<N - 1> child = b ? DefaultInitChain<N - 1>{} : DefaultInitChain<N - 1>{};
};
template <> struct DefaultInitChain<0> {
  int value = 0;
};
void useDefaultInitChain() { DefaultInitChain<40> chain{}; }

// The default member initializers of each member are rewritten for the
// aggregate they initialize when they materialize a temporary, which it
// extends, so each is visited, and each temporary is extended.
namespace DistinctDefaultInits {
struct Entry { const int &Value = 42; };
struct Pair { Entry First, Second; };
constexpr const Pair &P = Pair{};
static_assert(P.First.Value == 42);
static_assert(P.Second.Value == 42);
}

// A local reference reached first through what reads it as a value is still
// visited where something takes an address in it.
namespace ReadThenAddressed {
struct State {
  const int *Global;
  int Value;
};
const int *find(bool B) {
  const State &S = State{};
  const int *const P = B ? S.Global : &S.Value;
  return P; // expected-warning {{returning address of local temporary object}}
}
const int *findReversed(bool B) {
  const State &S = State{};
  const int *const P = B ? &S.Value : S.Global;
  return P; // expected-warning {{returning address of local temporary object}}
}
}

// A default argument a lambda capture binds through two arguments of a call,
// one after the initializer of a variable, which doesn't warn there, and one
// without, which does: the path before the default argument is the
// variable's in one of them.
namespace DefaultThroughVariable {
const int &f(const int &X [[clang::lifetimebound]] = 0);
const int &g(const int &A [[clang::lifetimebound]],
             const int &B [[clang::lifetimebound]]);
void use(...);
void before() {
  const int &R0 = f(); // expected-warning {{temporary bound to local reference 'R0' will be destroyed at the end of the full-expression}}
  auto L = [&X = g(R0, f())] { return X; }; // expected-warning {{temporary whose address is used as value of local variable 'L' will be destroyed at the end of the full-expression}}
  use(R0, L);
}
void after() {
  const int &R0 = f(); // expected-warning {{temporary bound to local reference 'R0' will be destroyed at the end of the full-expression}}
  auto L = [&X = g(f(), R0)] { return X; }; // expected-warning {{temporary whose address is used as value of local variable 'L' will be destroyed at the end of the full-expression}}
  use(R0, L);
}
}

// A default argument used at two places of an expression warns at each.
namespace DefaultUsedTwice {
const int &f(const int &V [[clang::lifetimebound]] = 42);
const int &pick(const int &A [[clang::lifetimebound]],
                const int &B [[clang::lifetimebound]]);
int conditional(bool B) {
  const int &R = B ? f() : f(); // expected-warning 2 {{temporary bound to local reference 'R' will be destroyed at the end of the full-expression}}
  return R;
}
int call() {
  const int &R = pick(f(), f()); // expected-warning 2 {{temporary bound to local reference 'R' will be destroyed at the end of the full-expression}}
  return R;
}
const int &returned(bool B) {
  return B ? f() : f(); // expected-warning 2 {{returning reference to local temporary object}}
}
}

// The initializer of a const variable read in the lookup of another reports
// what it finds in either order of the arms of a conditional.
namespace NestedConstLookup {
struct State { const int *p; int n; };
const int *first(bool B) {
  const State s{};
  const State *const sp = &s;
  const int *const p = B ? sp->p : &sp->n;
  return p; // expected-warning {{address of stack memory associated with local variable 's' returned}}
}
const int *second(bool B) {
  const State s{};
  const State *const sp = &s;
  const int *const p = B ? &sp->n : sp->p;
  return p; // expected-warning {{address of stack memory associated with local variable 's' returned}}
}
}
