// RUN: %clang_cc1 -std=c++20 -fsyntax-only -fconstexpr-steps=4000000000 -verify %s
// RUN: %clang_cc1 -std=c++20 -fsyntax-only -fconstexpr-steps=0 -verify %s

// The check of an expression for overflow evaluates what nothing needs the
// value of, like a chain of default arguments that each call the function of
// the one before twice, whose evaluation doubles with each, which a large
// bound of a constant expression, or none, lets go on: the value of a default
// argument that is the same at each use is evaluated once in the check, which
// still finds an overflow, also one after a long evaluation.

constexpr int f0(int = 0) { return 0; }
constexpr int f1(int = f0() + f0()) { return 0; }
constexpr int f2(int = f1() + f1()) { return 0; }
constexpr int f3(int = f2() + f2()) { return 0; }
constexpr int f4(int = f3() + f3()) { return 0; }
constexpr int f5(int = f4() + f4()) { return 0; }
constexpr int f6(int = f5() + f5()) { return 0; }
constexpr int f7(int = f6() + f6()) { return 0; }
constexpr int f8(int = f7() + f7()) { return 0; }
constexpr int f9(int = f8() + f8()) { return 0; }
constexpr int f10(int = f9() + f9()) { return 0; }
constexpr int f11(int = f10() + f10()) { return 0; }
constexpr int f12(int = f11() + f11()) { return 0; }
constexpr int f13(int = f12() + f12()) { return 0; }
constexpr int f14(int = f13() + f13()) { return 0; }
constexpr int f15(int = f14() + f14()) { return 0; }
constexpr int f16(int = f15() + f15()) { return 0; }
constexpr int f17(int = f16() + f16()) { return 0; }
constexpr int f18(int = f17() + f17()) { return 0; }
constexpr int f19(int = f18() + f18()) { return 0; }
constexpr int f20(int = f19() + f19()) { return 0; }
constexpr int f21(int = f20() + f20()) { return 0; }
constexpr int f22(int = f21() + f21()) { return 0; }
constexpr int f23(int = f22() + f22()) { return 0; }
constexpr int f24(int = f23() + f23()) { return 0; }
constexpr int f25(int = f24() + f24()) { return 0; }
constexpr int f26(int = f25() + f25()) { return 0; }
constexpr int f27(int = f26() + f26()) { return 0; }
constexpr int f28(int = f27() + f27()) { return 0; }
constexpr int f29(int = f28() + f28()) { return 0; }
constexpr int f30(int = f29() + f29()) { return 0; }
constexpr int f31(int = f30() + f30()) { return 0; }
constexpr int f32(int = f31() + f31()) { return 0; }
constexpr int f33(int = f32() + f32()) { return 0; }
constexpr int f34(int = f33() + f33()) { return 0; }
constexpr int f35(int = f34() + f34()) { return 0; }
constexpr int f36(int = f35() + f35()) { return 0; }
constexpr int f37(int = f36() + f36()) { return 0; }
constexpr int f38(int = f37() + f37()) { return 0; }
constexpr int f39(int = f38() + f38()) { return 0; }
constexpr int f40(int = f39() + f39()) { return 0; }

template <int N> struct S {
  static constexpr int f(int = S<N - 1>::f() + S<N - 1>::f()) { return 0; }
};
template <> struct S<0> {
  static constexpr int f(int = 0) { return 0; }
};
void g() { S<40>::f(); }

// So is the number of the temporary a reference parameter binds, which each
// use makes its own of.
constexpr int r0(int const &v = 0) { return v; }
constexpr int r1(int const &v = r0() + r0()) { return v; }
constexpr int r2(int const &v = r1() + r1()) { return v; }
constexpr int r3(int const &v = r2() + r2()) { return v; }
constexpr int r4(int const &v = r3() + r3()) { return v; }
constexpr int r5(int const &v = r4() + r4()) { return v; }
constexpr int r6(int const &v = r5() + r5()) { return v; }
constexpr int r7(int const &v = r6() + r6()) { return v; }
constexpr int r8(int const &v = r7() + r7()) { return v; }
constexpr int r9(int const &v = r8() + r8()) { return v; }
constexpr int r10(int const &v = r9() + r9()) { return v; }
constexpr int r11(int const &v = r10() + r10()) { return v; }
constexpr int r12(int const &v = r11() + r11()) { return v; }
constexpr int r13(int const &v = r12() + r12()) { return v; }
constexpr int r14(int const &v = r13() + r13()) { return v; }
constexpr int r15(int const &v = r14() + r14()) { return v; }
constexpr int r16(int const &v = r15() + r15()) { return v; }
constexpr int r17(int const &v = r16() + r16()) { return v; }
constexpr int r18(int const &v = r17() + r17()) { return v; }
constexpr int r19(int const &v = r18() + r18()) { return v; }
constexpr int r20(int const &v = r19() + r19()) { return v; }
constexpr int r21(int const &v = r20() + r20()) { return v; }
constexpr int r22(int const &v = r21() + r21()) { return v; }
constexpr int r23(int const &v = r22() + r22()) { return v; }
constexpr int r24(int const &v = r23() + r23()) { return v; }
constexpr int r25(int const &v = r24() + r24()) { return v; }
constexpr int r26(int const &v = r25() + r25()) { return v; }
constexpr int r27(int const &v = r26() + r26()) { return v; }
constexpr int r28(int const &v = r27() + r27()) { return v; }
constexpr int r29(int const &v = r28() + r28()) { return v; }
constexpr int r30(int const &v = r29() + r29()) { return v; }
constexpr int r31(int const &v = r30() + r30()) { return v; }
constexpr int r32(int const &v = r31() + r31()) { return v; }
constexpr int r33(int const &v = r32() + r32()) { return v; }
constexpr int r34(int const &v = r33() + r33()) { return v; }
constexpr int r35(int const &v = r34() + r34()) { return v; }
constexpr int r36(int const &v = r35() + r35()) { return v; }
constexpr int r37(int const &v = r36() + r36()) { return v; }
constexpr int r38(int const &v = r37() + r37()) { return v; }
constexpr int r39(int const &v = r38() + r38()) { return v; }
constexpr int r40(int const &v = r39() + r39()) { return v; }
int r() { return r40(); }

// So is a reference to an object with static storage duration, the lvalue a
// default argument is, of a chain of functions that return a reference to a
// global.
constexpr int g_Global = 1;
template <int N> struct SGlobalRef {
  static constexpr int const &f(int const &x = (SGlobalRef<N - 1>::f(),
                                                 SGlobalRef<N - 1>::f())) {
    return x;
  }
};
template <> struct SGlobalRef<0> {
  static constexpr int const &f(int const &x = g_Global) { return x; }
};
int globalRef() { return SGlobalRef<40>::f() + 1; }

// So is a pointer, to an object with static storage duration.
int g_Data;
constexpr int *choose(int *a, int *) { return a; }
template <int N> struct P {
  static constexpr int *f(int *p = choose(P<N - 1>::f(), P<N - 1>::f())) {
    return p;
  }
};
template <> struct P<0> {
  static constexpr int *f(int *p = &g_Data) { return p; }
};
int p() { return *P<40>::f() + 1; }

// And a class whose copy is its value.
struct Value {
  int n;
};
constexpr Value add(Value a, Value b) { return {a.n + b.n}; }
constexpr Value v0(Value v = {0}) { return v; }
constexpr Value v1(Value v = add(v0(), v0())) { return v; }
constexpr Value v2(Value v = add(v1(), v1())) { return v; }
constexpr Value v3(Value v = add(v2(), v2())) { return v; }
constexpr Value v4(Value v = add(v3(), v3())) { return v; }
constexpr Value v5(Value v = add(v4(), v4())) { return v; }
constexpr Value v6(Value v = add(v5(), v5())) { return v; }
constexpr Value v7(Value v = add(v6(), v6())) { return v; }
constexpr Value v8(Value v = add(v7(), v7())) { return v; }
constexpr Value v9(Value v = add(v8(), v8())) { return v; }
constexpr Value v10(Value v = add(v9(), v9())) { return v; }
constexpr Value v11(Value v = add(v10(), v10())) { return v; }
constexpr Value v12(Value v = add(v11(), v11())) { return v; }
constexpr Value v13(Value v = add(v12(), v12())) { return v; }
constexpr Value v14(Value v = add(v13(), v13())) { return v; }
constexpr Value v15(Value v = add(v14(), v14())) { return v; }
constexpr Value v16(Value v = add(v15(), v15())) { return v; }
constexpr Value v17(Value v = add(v16(), v16())) { return v; }
constexpr Value v18(Value v = add(v17(), v17())) { return v; }
constexpr Value v19(Value v = add(v18(), v18())) { return v; }
constexpr Value v20(Value v = add(v19(), v19())) { return v; }
constexpr Value v21(Value v = add(v20(), v20())) { return v; }
constexpr Value v22(Value v = add(v21(), v21())) { return v; }
constexpr Value v23(Value v = add(v22(), v22())) { return v; }
constexpr Value v24(Value v = add(v23(), v23())) { return v; }
constexpr Value v25(Value v = add(v24(), v24())) { return v; }
constexpr Value v26(Value v = add(v25(), v25())) { return v; }
constexpr Value v27(Value v = add(v26(), v26())) { return v; }
constexpr Value v28(Value v = add(v27(), v27())) { return v; }
constexpr Value v29(Value v = add(v28(), v28())) { return v; }
constexpr Value v30(Value v = add(v29(), v29())) { return v; }
constexpr Value v31(Value v = add(v30(), v30())) { return v; }
constexpr Value v32(Value v = add(v31(), v31())) { return v; }
constexpr Value v33(Value v = add(v32(), v32())) { return v; }
constexpr Value v34(Value v = add(v33(), v33())) { return v; }
constexpr Value v35(Value v = add(v34(), v34())) { return v; }
constexpr Value v36(Value v = add(v35(), v35())) { return v; }
constexpr Value v37(Value v = add(v36(), v36())) { return v; }
constexpr Value v38(Value v = add(v37(), v37())) { return v; }
constexpr Value v39(Value v = add(v38(), v38())) { return v; }
constexpr Value v40(Value v = add(v39(), v39())) { return v; }
int v() { return v40().n + 1; }

// Also of a class with a copy constructor of its own.
struct VCopied {
  int x;
  constexpr VCopied(int n) : x(n) {}
  constexpr VCopied(VCopied const &v) : x(v.x) {}
};
constexpr VCopied addCopied(VCopied a, VCopied b) { return VCopied{a.x + b.x}; }
template <int N> struct TCopied {
  static constexpr VCopied f(VCopied v = addCopied(TCopied<N - 1>::f(),
                                                   TCopied<N - 1>::f())) {
    return v;
  }
};
template <> struct TCopied<0> {
  static constexpr VCopied f(VCopied v = VCopied{0}) { return v; }
};
int copied() { return TCopied<40>::f().x + 1; }

// And of a class with a constexpr destructor of its own.
struct VDestroyed {
  int x;
  constexpr ~VDestroyed() {}
};
template <int N> struct TDestroyed {
  static constexpr VDestroyed f(VDestroyed v = {TDestroyed<N - 1>::f().x +
                                                TDestroyed<N - 1>::f().x}) {
    return v;
  }
};
template <> struct TDestroyed<0> {
  static constexpr VDestroyed f(VDestroyed v = {0}) { return v; }
};
VDestroyed destroyed() { return TDestroyed<40>::f(); }

// And a default member initializer that doesn't refer to the object it
// initializes, which has the same value at each use too.
template <int N> struct SSum {
  int x = SSum<N - 1>{}.x + SSum<N - 1>{}.x;
};
template <> struct SSum<0> {
  int x = 0;
};
int sum() { return SSum<40>{}.x + 1; }

// Also one that only refers to it in operands that aren't evaluated.
template <int N> struct SUnevaluated {
  int x = int(sizeof(this) - sizeof(this)) +
          int(alignof(decltype(*this)) - alignof(int)) +
          int(noexcept(this->x) - true) + int(sizeof(this->x) - sizeof(int)) +
          SUnevaluated<N - 1>{}.x + SUnevaluated<N - 1>{}.x;
};
template <> struct SUnevaluated<0> {
  int x = 0;
};
int unevaluated() { return SUnevaluated<40>{}.x + 1; }

// So is one whose evaluation fails, like a call of a function that isn't
// constexpr, which the discarded left side of a comma doesn't stop the
// evaluation of the right side at.
template <int N> struct SFailed {
  static int f(int = (SFailed<N - 1>::f(), SFailed<N - 1>::f()));
};
template <> struct SFailed<0> {
  static int f(int = 1);
};
void failed() { SFailed<40>::f(); }

// And a default member initializer whose evaluation fails.
int notConstant();
template <int N> struct SFailedInit {
  int x = ((void)SFailedInit<N - 1>{}.x, SFailedInit<N - 1>{}.x);
};
template <> struct SFailedInit<0> {
  int x = notConstant();
};
int failedInit() { return SFailedInit<40>{}.x + 1; }

// Also through a comma, whose left side is evaluated with it.
template <int N> struct SCommaRef {
  static constexpr int f(int const &n = (SCommaRef<N - 1>::f(),
                                         SCommaRef<N - 1>::f())) {
    return n;
  }
};
template <> struct SCommaRef<0> {
  static constexpr int f(int const &n = __INT_MAX__) { return n; }
};
int commaRef() { return SCommaRef<40>::f() + 1; } // expected-warning {{overflow in expression; result is -2'147'483'648 with type 'int'}}

// So is the temporary of a reference parameter whose evaluation fails.
int unknown();
template <int N> struct SFailedRef {
  static int f(int const &n = (SFailedRef<N - 1>::f(), SFailedRef<N - 1>::f()));
};
template <> struct SFailedRef<0> {
  static int f(int const &n = unknown());
};
void failedRef() { SFailedRef<40>::f(); }

// But one that failed because it went past the limit of the depth of calls
// fails again only as deep: a later use, less deep, is checked.
constexpr int late();
constexpr int deep0(int v = late() + 1) { return v; } // expected-warning {{overflow in expression; result is -2'147'483'648 with type 'int'}}
constexpr int recurse(int n) { return n ? recurse(n - 1) : deep0(); }
constexpr int late() { return __INT_MAX__; }
int deep() { return recurse(511) + deep0(); }

// But not one that does, which can overflow for one object and not another.
struct SRelative {
  int Base;
  int Next = Base + 1; // expected-warning {{overflow in expression; result is -2'147'483'648 with type 'int'}}
};
int relative() { return SRelative{0}.Next + SRelative{__INT_MAX__}.Next; }

// So is one with a source location, which has the location of the use of the
// outermost default argument it is in, which each use in it shares.
template <int N> struct L {
  static constexpr int f(int = L<N - 1>::f() + L<N - 1>::f()) { return 0; }
};
template <> struct L<0> {
  static constexpr int f(int = __builtin_LINE()) { return 0; }
};
void l() { L<40>::f(); }

// But not the uses of others, of other lines.
constexpr int at(int Line = __builtin_LINE()) { return Line; }
constexpr int nested(int Line = at()) { return Line; }
int lines() {
  return (nested() - __LINE__) +
         (__LINE__ - nested()) + __INT_MAX__;
}

constexpr int g(int = __INT_MAX__ + 1) { return 0; } // expected-warning {{overflow in expression; result is -2'147'483'648 with type 'int'}}

constexpr int count(int x) {
  for (int i = 0; i < 80000; ++i)
    ++x;
  return x;
}
int h() { return count(1) + __INT_MAX__; } // expected-warning {{overflow in expression; result is -2'147'403'648 with type 'int'}}

// So is the value of a complex or vector number.
template <int N> struct SComplex {
  static constexpr _Complex double
  f(_Complex double v = SComplex<N - 1>::f() + SComplex<N - 1>::f()) {
    return v;
  }
};
template <> struct SComplex<0> {
  static constexpr _Complex double f(_Complex double v = 0) { return v; }
};
void useComplex() { SComplex<40>::f(); }

using Vector = int __attribute__((ext_vector_type(4)));
template <int N> struct SVector {
  static constexpr Vector f(Vector v = SVector<N - 1>::f() + SVector<N - 1>::f()) {
    return v;
  }
};
template <> struct SVector<0> {
  static constexpr Vector f(Vector v = Vector()) { return v; }
};
void useVector() { SVector<40>::f(); }
