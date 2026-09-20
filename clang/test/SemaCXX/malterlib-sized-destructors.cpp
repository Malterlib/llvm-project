// RUN: %clang_cc1 -std=c++20 -triple x86_64-linux-gnu -fmalterlib-sized-destructors -fsyntax-only -verify %s
// RUN: %clang_cc1 -std=c++20 -triple x86_64-linux-gnu -fsyntax-only -verify=disabled -DDISABLED %s
// The Microsoft ABI selects the destructor's mode with its flags argument, not
// with bit 0 of 'this', so packed classes are fine there.
// RUN: %clang_cc1 -std=c++20 -triple x86_64-pc-windows-msvc -fms-extensions -fmalterlib-sized-destructors -fsyntax-only -verify=ms -DMS %s

struct Virtual {
  virtual ~Virtual();
  int m;
};

struct Derived : Virtual {
  ~Derived() override;
};

struct NonVirtual {
  ~NonVirtual();
};

struct Incomplete;
#if !defined(DISABLED) && !defined(MS)
// expected-note@-2 {{forward declaration of 'Incomplete'}}
#endif

struct PrivateDtor {
  virtual void f();
private:
  virtual ~PrivateDtor();
#if !defined(DISABLED) && !defined(MS)
  // expected-note@-2 {{declared private here}}
#endif
};

#ifdef DISABLED
static_assert(!__has_feature(malterlib_sized_destructors));
static_assert(!__has_malterlib_sized_destructor(Virtual));
static_assert(!__has_malterlib_sized_destructor(NonVirtual));

unsigned long disabled(Virtual *p, void **memory) {
  return __builtin_malterlib_destroy(p, memory); // disabled-error {{'__builtin_malterlib_destroy' requires '-fmalterlib-sized-destructors'}}
}
#elif !defined(MS)

static_assert(__has_feature(malterlib_sized_destructors));
static_assert(__has_malterlib_sized_destructor(Virtual));
static_assert(__has_malterlib_sized_destructor(Derived));
static_assert(!__has_malterlib_sized_destructor(NonVirtual));
static_assert(!__has_malterlib_sized_destructor(int));
static_assert(!__has_malterlib_sized_destructor(Virtual *));

unsigned long ok(Virtual *p, void **memory) {
  return __builtin_malterlib_destroy(p, memory);
}

template <typename T>
unsigned long dependent(T *p, void **memory) {
  return __builtin_malterlib_destroy(p, memory);
}

unsigned long instantiate(Derived *p, void **memory) {
  return dependent(p, memory);
}

void errors(Virtual *p, void **memory, NonVirtual *nonVirtual, void *opaque,
            Incomplete *incomplete, int i) {
  __builtin_malterlib_destroy(p); // expected-error {{too few arguments to function call, expected 2, have 1}}
  __builtin_malterlib_destroy(p, memory, memory); // expected-error {{too many arguments to function call, expected 2, have 3}}
  __builtin_malterlib_destroy(i, memory); // expected-error {{first argument to '__builtin_malterlib_destroy' must be a pointer to a class with a virtual destructor, not 'int'}}
  __builtin_malterlib_destroy(opaque, memory); // expected-error {{first argument to '__builtin_malterlib_destroy' must be a pointer to a class with a virtual destructor, not 'void *'}}
  __builtin_malterlib_destroy(nonVirtual, memory); // expected-error {{first argument to '__builtin_malterlib_destroy' must be a pointer to a class with a virtual destructor, not 'NonVirtual *'}}
  __builtin_malterlib_destroy(incomplete, memory); // expected-error {{incomplete type 'Incomplete' where a complete type is required}}
  __builtin_malterlib_destroy(p, i); // expected-error {{cannot initialize a parameter of type 'void **' with an lvalue of type 'int'}}
}

void accessError(PrivateDtor *p, void **memory) {
  __builtin_malterlib_destroy(p, memory); // expected-error {{calling a private destructor of class 'PrivateDtor'}}
}

unsigned long sizeIsSizeT(Virtual *p, void **memory) {
  return __builtin_malterlib_destroy(p, memory);
}

static_assert(__is_same(decltype(__builtin_malterlib_destroy((Virtual *)0, (void **)0)), decltype(sizeof(0))));

// The builtin throws what the destructor it calls throws.
struct Throwing {
  virtual ~Throwing() noexcept(false);
};
static_assert(noexcept(__builtin_malterlib_destroy((Virtual *)0, (void **)0)));
static_assert(!noexcept(__builtin_malterlib_destroy((Throwing *)0, (void **)0)));
#endif

// A deleting destructor's mode lives in bit 0 of 'this', so a polymorphic
// class needs an alignment of at least 2.
struct __attribute__((packed)) Packed { // expected-error {{'Packed' has a virtual destructor but an alignment below 2, which '-fmalterlib-sized-destructors' needs to select the destructor's mode}}
  virtual ~Packed();
  char c;
};

struct __attribute__((packed, aligned(2))) PackedAligned {
  virtual ~PackedAligned();
  char c;
};

// A base subobject the destructor can be called through must be at an even
// offset as well: a packed derived class places the second base right after
// the first one's data, at an odd offset.
struct PackedBase1 {
  virtual ~PackedBase1();
  char c;
};
struct PackedBase2 {
  virtual ~PackedBase2();
  char c;
};
// A virtual base of the type of a direct nonvirtual base is another subobject,
// which the complete class places.
struct SameTypeBase {
  virtual ~SameTypeBase();
  int i;
};
struct SameTypeVirtual : virtual SameTypeBase {}; // #same-type-virtual
#pragma pack(push, 1)
struct alignas(2) OddBase : PackedBase1, PackedBase2 { // expected-error {{'OddBase' has its base 'PackedBase2', which holds a class with a virtual destructor, at the odd offset 9}}
  ~OddBase();
};
struct alignas(2) OddVirtualBase : PackedBase1, virtual PackedBase2 { // expected-error {{'OddVirtualBase' has its base 'PackedBase2', which holds a class with a virtual destructor, at the odd offset 9}}
  ~OddVirtualBase();
};
struct alignas(2) OddSameTypeVirtual : SameTypeBase, SameTypeVirtual { // expected-warning {{direct base 'SameTypeBase' is inaccessible due to ambiguity}} disabled-warning {{direct base 'SameTypeBase' is inaccessible due to ambiguity}} ms-warning {{direct base 'SameTypeBase' is inaccessible due to ambiguity}}
  // expected-error@#same-type-virtual {{'OddSameTypeVirtual' has its base 'SameTypeBase', which holds a class with a virtual destructor, at the odd offset 21}}
  char c;
};
struct alignas(2) EvenBase : PackedBase1 {
  char pad;
  ~EvenBase();
};
struct alignas(8) OddMember {
  char pad;
  PackedBase1 member; // expected-error {{'OddMember' has its member 'member', which holds a class with a virtual destructor, at the odd offset 1}}
};
struct alignas(8) EvenMember {
  short pad;
  PackedBase1 member;
  PackedBase1 members[2];
};
struct UnalignedHolder { // expected-error {{'UnalignedHolder' holds a class with a virtual destructor but an alignment below 2}}
  PackedBase1 member;
  char tail;
};
#pragma pack(pop)

// A member that only nests such a class counts as one.
struct Holder {
  PackedBase1 member;
};
#pragma pack(push, 1)
struct alignas(2) OddHolder {
  char pad;
  Holder holder; // expected-error {{'OddHolder' has its member 'holder', which holds a class with a virtual destructor, at the odd offset 1}}
};
struct alignas(2) OddHolderBase : PackedBase1, Holder { // expected-error {{'OddHolderBase' has its base 'Holder', which holds a class with a virtual destructor, at the odd offset 9}}
};
struct UnalignedDerived : Holder { // expected-error {{'UnalignedDerived' holds a class with a virtual destructor but an alignment below 2}}
  char tail;
};
#pragma pack(pop)

template <typename T> struct __attribute__((packed)) PackedTemplate { // expected-error {{'PackedTemplate<char>' has a virtual destructor but an alignment below 2}}
  virtual ~PackedTemplate();
  T t;
};
PackedTemplate<char> packedTemplate; // expected-note {{in instantiation of template class 'PackedTemplate<char>' requested here}}

// The size comes back in the register these conventions have the callee
// preserve. The Microsoft ABI does not let a destructor use them at all.
struct PreserveMost {
  __attribute__((preserve_most)) virtual ~PreserveMost(); // expected-error {{the virtual destructor of 'PreserveMost' uses the 'preserve_most' calling convention, which preserves a register '-fmalterlib-sized-destructors' returns the size in}} ms-warning {{preserve_most calling convention is not supported on constructor/destructor}}
};
struct PreserveAll {
  __attribute__((preserve_all)) virtual ~PreserveAll(); // expected-error {{uses the 'preserve_all' calling convention}} ms-warning {{preserve_all calling convention is not supported on constructor/destructor}}
};
struct PreserveNone {
  __attribute__((preserve_none)) virtual ~PreserveNone(); // ms-warning {{preserve_none calling convention is not supported on constructor/destructor}}
};
struct NoCallerSaved {
  __attribute__((no_caller_saved_registers)) virtual ~NoCallerSaved(); // expected-error {{the virtual destructor of 'NoCallerSaved' has the 'no_caller_saved_registers' attribute, which preserves a register '-fmalterlib-sized-destructors' returns the size in}} ms-error {{has the 'no_caller_saved_registers' attribute}}
};

// An object a call produces is constructed by the callee, with the vtable of
// the callee's image, which the link cannot check.
struct Produced {
  virtual ~Produced();
  Produced();
  Produced(Produced &&);
};
Produced produce();
Produced &&forward(Produced &&);
[[malterlib::sized_construction]] Produced *constructProduced() {
  Produced p;
  new Produced(static_cast<Produced &&>(p));
  new Produced(forward(produce()));
  new Produced(Produced());
  new Produced((Produced()));
  new Produced(static_cast<Produced>(Produced()));
  new Produced(Produced(static_cast<Produced &&>(p)));
  new Produced(Produced(produce())); // expected-error {{cannot construct 'Produced' from a value another function produces}} ms-error {{cannot construct 'Produced' from a value another function produces}}
  new Produced{Produced{}};
  new Produced{Produced{produce()}}; // expected-error {{cannot construct 'Produced' from a value another function produces}} ms-error {{cannot construct 'Produced' from a value another function produces}}
  new Produced(produce()); // expected-error {{a function with the 'malterlib::sized_construction' attribute cannot construct 'Produced' from a value another function produces, whose vtable '-fmalterlib-sized-destructors' cannot check; construct it with a constructor}} ms-error {{cannot construct 'Produced' from a value another function produces}}
  return new Produced((produce())); // expected-error {{cannot construct 'Produced' from a value another function produces}} ms-error {{cannot construct 'Produced' from a value another function produces}}
}
Produced *constructProducedElsewhere() { return new Produced(produce()); }
// So is each element of an array, and each result of a conditional.
bool alternate();
[[malterlib::sized_construction]] Produced *constructProducedArray(const bool *b) {
  new Produced[2];
  new Produced[2]{Produced(), Produced()};
  new Produced[2]{alternate() ? Produced() : Produced(), Produced()};
  new Produced(alternate() ? Produced() : Produced());
  new Produced{alternate() ? Produced() : Produced()};
  new Produced[2]{(alternate(), Produced()), Produced()};
  new Produced((alternate(), produce())); // expected-error {{cannot construct 'Produced' from a value another function produces}} ms-error {{cannot construct 'Produced' from a value another function produces}}
  new Produced(Produced(alternate() ? Produced() : Produced()));
  new Produced(alternate() ? Produced() : produce()); // expected-error {{cannot construct 'Produced' from a value another function produces}} ms-error {{cannot construct 'Produced' from a value another function produces}}
  // Not one a constant condition doesn't choose, which isn't evaluated.
  new Produced(true ? Produced() : produce());
  new Produced[2]{false ? produce() : Produced(), Produced()};
  new Produced(false ? Produced() : produce()); // expected-error {{cannot construct 'Produced' from a value another function produces}} ms-error {{cannot construct 'Produced' from a value another function produces}}
  // Also one whose call the operand before it leaves out, `c || f()`, and one
  // that tells whether it is constant evaluated, which it isn't where the
  // program runs, but not one whose call is evaluated first.
  new Produced((sizeof(void *) > 0 || alternate()) ? Produced() : produce());
  new Produced((sizeof(void *) == 0 && alternate()) ? produce() : Produced());
  new Produced(__builtin_is_constant_evaluated() ? produce() : Produced());
  new Produced((alternate() || true) ? Produced() : produce()); // expected-error {{cannot construct 'Produced' from a value another function produces}} ms-error {{cannot construct 'Produced' from a value another function produces}}
  // Each operand of a long chain is looked at once.
  new Produced((b[0] && b[1] && b[2] && b[3] && b[4] && b[5] && b[6] && b[7] &&
                b[8] && b[9] && b[10] && b[11] && b[12] && b[13] && b[14] &&
                b[15] && b[16] && b[17] && b[18] && b[19] && b[20] && b[21] &&
                b[22] && b[23] && b[24] && b[25] && b[26] && b[27] && b[28] &&
                b[29] && b[30] && b[31] && b[32] && b[33] && b[34] && b[35] &&
                b[36] && b[37] && b[38] && b[39])
                   ? Produced()
                   : Produced());
  return new Produced[2]{Produced(), produce()}; // expected-error {{cannot construct 'Produced' from a value another function produces}} ms-error {{cannot construct 'Produced' from a value another function produces}}
}
struct Polymorphic {
  virtual void f();
};
Polymorphic producePolymorphic();
[[malterlib::sized_construction]] Polymorphic *constructPolymorphic() {
  return new Polymorphic(producePolymorphic());
}

// An unevaluated operand constructs nothing.
[[malterlib::sized_construction]] void queryPolymorphic() {
  using Result = decltype(new Polymorphic(producePolymorphic())); // expected-warning {{expression with side effects has no effect in an unevaluated context}} disabled-warning {{expression with side effects has no effect in an unevaluated context}} ms-warning {{expression with side effects has no effect in an unevaluated context}}
  static_assert(noexcept(new Polymorphic(producePolymorphic())) || true); // expected-warning {{expression with side effects has no effect in an unevaluated context}} disabled-warning {{expression with side effects has no effect in an unevaluated context}} ms-warning {{expression with side effects has no effect in an unevaluated context}}
}

// A default argument, also a default member initializer, that a site
// evaluates is checked there, and a template whose arguments choose the
// construction where it is instantiated.
void sinkProduced(Produced * = new Produced(produce())); // expected-error {{cannot construct 'Produced' from a value another function produces}} ms-error {{cannot construct 'Produced' from a value another function produces}}
struct CProducedHolder {
  Produced *m_pValue = new Produced(produce()); // expected-error {{cannot construct 'Produced' from a value another function produces}} ms-error {{cannot construct 'Produced' from a value another function produces}}
};
[[malterlib::sized_construction]] void evaluateDefaults() {
  sinkProduced();
  CProducedHolder Holder{};
  (void)Holder;
}
void evaluateDefaultsElsewhere() { sinkProduced(); }
template <bool Direct>
[[malterlib::sized_construction]] Produced *constructChosen() {
  return new Produced(Direct ? Produced() : produce()); // expected-error {{cannot construct 'Produced' from a value another function produces}} ms-error {{cannot construct 'Produced' from a value another function produces}}
}
Produced *g_pChosen = constructChosen<true>();
Produced *g_pProduced = constructChosen<false>(); // expected-note {{in instantiation of function template specialization}} ms-note {{in instantiation of function template specialization}}
// Also one a default argument evaluates in turn.
void sinkInner(Produced * = new Produced(produce())); // expected-error {{cannot construct 'Produced' from a value another function produces}} ms-error {{cannot construct 'Produced' from a value another function produces}}
void sinkOuter(int = (sinkInner(), 0));
[[malterlib::sized_construction]] void evaluateNestedDefaults() { sinkOuter(); }
// Also what a lambda one makes captures, which is evaluated there, and each
// default once, however many defaults share it, in linear time.
void sinkCaptured(int = [pValue = new Produced(produce())] { return 0; }()); // expected-error {{cannot construct 'Produced' from a value another function produces}} ms-error {{cannot construct 'Produced' from a value another function produces}}
[[malterlib::sized_construction]] void evaluateCaptures() { sinkCaptured(); }
// Also where the default is rebuilt where it is used, for a `consteval` call
// or a source location in it, which keeps the lambda as it is.
consteval unsigned countConsteval() { return 1; }
void sinkRebuilt(Produced * = [pValue = new Produced[countConsteval()]{produce()}] { return pValue; }()); // expected-error {{cannot construct 'Produced' from a value another function produces}} ms-error {{cannot construct 'Produced' from a value another function produces}}
[[malterlib::sized_construction]] void evaluateRebuilt() { sinkRebuilt(); }
void sinkLocated(int = [pValue = new Produced(produce())](int = __builtin_LINE()) { return 0; }()); // expected-error {{cannot construct 'Produced' from a value another function produces}} ms-error {{cannot construct 'Produced' from a value another function produces}}
[[malterlib::sized_construction]] void evaluateLocated() { sinkLocated(); }
void sinkShared(Produced * = new Produced(produce())); // expected-error {{cannot construct 'Produced' from a value another function produces}} ms-error {{cannot construct 'Produced' from a value another function produces}}
int sharedLevel0(int = (sinkShared(), 0));
#define SHARED_LEVEL(N, P) int sharedLevel##N(int = sharedLevel##P() + sharedLevel##P());
SHARED_LEVEL(1, 0)
SHARED_LEVEL(2, 1)
SHARED_LEVEL(3, 2)
SHARED_LEVEL(4, 3)
SHARED_LEVEL(5, 4)
SHARED_LEVEL(6, 5)
SHARED_LEVEL(7, 6)
SHARED_LEVEL(8, 7)
SHARED_LEVEL(9, 8)
SHARED_LEVEL(10, 9)
SHARED_LEVEL(11, 10)
SHARED_LEVEL(12, 11)
SHARED_LEVEL(13, 12)
SHARED_LEVEL(14, 13)
SHARED_LEVEL(15, 14)
SHARED_LEVEL(16, 15)
SHARED_LEVEL(17, 16)
SHARED_LEVEL(18, 17)
SHARED_LEVEL(19, 18)
SHARED_LEVEL(20, 19)
SHARED_LEVEL(21, 20)
SHARED_LEVEL(22, 21)
SHARED_LEVEL(23, 22)
SHARED_LEVEL(24, 23)
SHARED_LEVEL(25, 24)
SHARED_LEVEL(26, 25)
SHARED_LEVEL(27, 26)
SHARED_LEVEL(28, 27)
SHARED_LEVEL(29, 28)
SHARED_LEVEL(30, 29)
SHARED_LEVEL(31, 30)
SHARED_LEVEL(32, 31)
SHARED_LEVEL(33, 32)
SHARED_LEVEL(34, 33)
SHARED_LEVEL(35, 34)
SHARED_LEVEL(36, 35)
SHARED_LEVEL(37, 36)
SHARED_LEVEL(38, 37)
SHARED_LEVEL(39, 38)
SHARED_LEVEL(40, 39)
[[malterlib::sized_construction]] void evaluateShared() {
  sharedLevel40();
  sharedLevel40();
}
// Also a call in a result of a condition that a constant condition leaves
// out, `c_bUsePolicy ? fg_Policy() : true`, which doesn't keep the outer one
// from being constant.
bool fg_Policy();
constexpr bool c_bUsePolicy = false;
[[malterlib::sized_construction]] Produced *constructWithoutPolicy() {
  return new Produced((c_bUsePolicy ? fg_Policy() : true) ? Produced()
                                                          : produce());
}
// Not a branch of an `if constexpr` an instantiation discards, which is checked
// where it is instantiated.
template <bool Direct>
[[malterlib::sized_construction]] Produced *constructDiscarded() {
  if constexpr (Direct)
    return new Produced;
  else
    return new Produced(produce()); // expected-error {{cannot construct 'Produced' from a value another function produces}} ms-error {{cannot construct 'Produced' from a value another function produces}}
}
Produced *g_pDirect = constructDiscarded<true>();
Produced *g_pDiscarded = constructDiscarded<false>(); // expected-note {{in instantiation of function template specialization}} ms-note {{in instantiation of function template specialization}}
// A default doesn't construct in an operand that isn't evaluated, of
// `__uuidof` or of a builtin that only asks something of it, like
// `__builtin_constant_p`, nor in an association that `_Generic` doesn't
// choose.
#ifdef MS
struct __declspec(uuid("00000000-0000-0000-0000-000000000001")) Identified {
  virtual ~Identified();
};
Identified produceIdentified();
void queryIdentified(const void * = &__uuidof(*new Identified(produceIdentified())));
struct CIdentifiedHolder {
  const void *m_pId = &__uuidof(*new Identified(produceIdentified()));
};
[[malterlib::sized_construction]] void evaluateIdentified() {
  queryIdentified();
  CIdentifiedHolder Holder{};
  (void)Holder;
}
#endif
void sinkGeneric(Produced * = _Generic(0, int: (Produced *)nullptr, default: new Produced(produce())));
[[malterlib::sized_construction]] void evaluateGeneric() { sinkGeneric(); }
void sinkConstant(int = __builtin_constant_p(new Produced(produce())));
void sinkSize(decltype(sizeof(0)) = __builtin_object_size(new Produced(produce()), 0));
struct CConstantHolder {
  int m_bConstant = __builtin_constant_p(new Produced(produce()));
};
[[malterlib::sized_construction]] void evaluateQueries() {
  sinkConstant();
  sinkSize();
  CConstantHolder Holder{};
  (void)Holder;
}
// Nor does the body itself, or a template instantiated from it.
[[malterlib::sized_construction]] int queryInBody() {
  return __builtin_constant_p(new Produced(produce())) +
         _Generic(0, int: 0, default: (new Produced(produce()), 1));
}
template <class T>
[[malterlib::sized_construction]] decltype(sizeof(0)) queryInTemplate() {
  return __builtin_object_size(new T(produce()), 0);
}
decltype(sizeof(0)) g_QueriedSize = queryInTemplate<Produced>();
[[malterlib::sized_construction]] Produced *constructAfterQuery() {
  (void)__builtin_constant_p(new Produced(produce()));
  return new Produced(produce()); // expected-error {{cannot construct 'Produced' from a value another function produces}} ms-error {{cannot construct 'Produced' from a value another function produces}}
}
// Nor a discarded branch of an `if constexpr`, also outside of a template.
constexpr bool g_bUseForeignFactory = false;
[[malterlib::sized_construction]] Produced *constructChosenBranch() {
  if constexpr (g_bUseForeignFactory)
    return new Produced(produce());
  else
    return new Produced;
}
[[malterlib::sized_construction]] Produced *constructTakenBranch() {
  if constexpr (!g_bUseForeignFactory)
    return new Produced(produce()); // expected-error {{cannot construct 'Produced' from a value another function produces}} ms-error {{cannot construct 'Produced' from a value another function produces}}
  else
    return new Produced;
}
// Nor a default the body uses where it doesn't evaluate it, in a discarded
// branch or an operand that isn't evaluated, also a default member
// initializer.
void sinkDiscarded(Produced * = new Produced(produce()));
struct CDiscardedHolder {
  Produced *m_pValue = new Produced(produce());
};
[[malterlib::sized_construction]] int useDefaultsUnevaluated() {
  if constexpr (false) {
    sinkDiscarded();
    CDiscardedHolder Holder{};
    (void)Holder;
  }
  return __builtin_constant_p((sinkDiscarded(), 0)) +
         _Generic(0, int: 0, default: (sinkDiscarded(), 1));
}
// Nor the branch of an `if consteval` only constant evaluation runs, whose
// objects it destroys.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wc++23-extensions"
struct CConstant {
  constexpr CConstant() {}
  constexpr virtual ~CConstant() {}
};
constexpr CConstant produceConstant() { return {}; }
[[malterlib::sized_construction]] constexpr CConstant *constructConsteval() {
  if consteval {
    return new CConstant(produceConstant());
  } else {
    return new CConstant;
  }
}
[[malterlib::sized_construction]] constexpr CConstant *constructNotConsteval() {
  if !consteval {
    return new CConstant(produceConstant()); // expected-error {{cannot construct 'CConstant' from a value another function produces}} ms-error {{cannot construct 'CConstant' from a value another function produces}}
  } else {
    return new CConstant;
  }
}
#pragma clang diagnostic pop
// And of a condition a builtin that only asks something of its operand
// decides, `__builtin_constant_p(1)`, only the result it chooses.
[[malterlib::sized_construction]] Produced *constructConstantProbe() {
  return new Produced(__builtin_constant_p(1) ? Produced() : produce());
}
// Also a defaulted construction site, of the default member initializers it
// uses, and what initializes the elements past those an initializer list
// gives, of a default it evaluates.
struct CDefaultedHolder {
  Produced *m_pValue = new Produced(produce()); // expected-error {{cannot construct 'Produced' from a value another function produces}} ms-error {{cannot construct 'Produced' from a value another function produces}}
  [[malterlib::sized_construction]] CDefaultedHolder() = default;
};
CDefaultedHolder g_DefaultedHolder; // expected-note {{in defaulted default constructor for 'CDefaultedHolder' first required here}} ms-note {{in defaulted default constructor for 'CDefaultedHolder' first required here}}
struct CFilledHolder {
  CFilledHolder(Produced * = new Produced(produce())); // expected-error {{cannot construct 'Produced' from a value another function produces}} ms-error {{cannot construct 'Produced' from a value another function produces}}
  virtual ~CFilledHolder();
};
[[malterlib::sized_construction]] CFilledHolder *constructFilled(decltype(sizeof(0)) _nCount) {
  return new CFilledHolder[_nCount]{};
}
// Also what initializes the elements past those a parenthesized list gives,
// `new CParenHolder[n](CParenHolder(nullptr))`.
struct CParenHolder {
  CParenHolder(Produced * = new Produced(produce())); // expected-error {{cannot construct 'Produced' from a value another function produces}} ms-error {{cannot construct 'Produced' from a value another function produces}}
  virtual ~CParenHolder();
};
[[malterlib::sized_construction]] CParenHolder *constructParenFilled(decltype(sizeof(0)) _nCount) {
  return new CParenHolder[_nCount](CParenHolder(nullptr));
}
// Nor what an immediate invocation constructs, which never runs, nor the
// operand `__builtin_choose_expr` doesn't choose.
struct CCompileTime {
  constexpr CCompileTime(int = 0) {}
  constexpr virtual ~CCompileTime() {}
};
constexpr CCompileTime produceCompileTime() { return {}; }
consteval int tagCompileTime(CCompileTime *p) {
  delete p;
  return 1;
}
[[malterlib::sized_construction]] CCompileTime *constructWithImmediate() {
  return new CCompileTime(tagCompileTime(new CCompileTime(produceCompileTime())));
}
[[malterlib::sized_construction]] Produced *constructChosen() {
  return __builtin_choose_expr(sizeof(void *) == 8, new Produced, new Produced(produce()));
}
[[malterlib::sized_construction]] Produced *constructChosenProduced() {
  return __builtin_choose_expr(sizeof(void *) == 8, new Produced(produce()), new Produced); // expected-error {{cannot construct 'Produced' from a value another function produces}} ms-error {{cannot construct 'Produced' from a value another function produces}}
}
// Nor what would initialize the elements of an array that has none, `new
// CNoneHolder[0]`, nor its default arguments, which never run, also in an
// instantiation with no elements.
struct CNoneHolder {
  CNoneHolder(Produced * = new Produced(produce()));
  virtual ~CNoneHolder();
};
[[malterlib::sized_construction]] CNoneHolder *constructNone() {
  return new CNoneHolder[0];
}
[[malterlib::sized_construction]] auto *constructNoneInner(decltype(sizeof(0)) _nCount) {
  return new CNoneHolder[_nCount][0];
}
template <decltype(sizeof(0)) tf_nCount>
[[malterlib::sized_construction]] CNoneHolder *constructNoneOf() {
  return new CNoneHolder[tf_nCount];
}
template CNoneHolder *constructNoneOf<0>();
// Also what a defaulted copy or move construction site constructs, with the
// default arguments of the constructors of the members it calls.
struct CCopied {
  CCopied(const CCopied &, Produced * = new Produced(produce())); // expected-error {{cannot construct 'Produced' from a value another function produces}} ms-error {{cannot construct 'Produced' from a value another function produces}}
  CCopied(CCopied &&, Produced * = new Produced(produce())); // expected-error {{cannot construct 'Produced' from a value another function produces}} ms-error {{cannot construct 'Produced' from a value another function produces}}
};
struct CCopyHolder {
  CCopied m_Copied;
  [[malterlib::sized_construction]] CCopyHolder(const CCopyHolder &) = default;
  [[malterlib::sized_construction]] CCopyHolder(CCopyHolder &&) = default;
};
CCopyHolder *copyHolder(const CCopyHolder &_Holder) { return new CCopyHolder(_Holder); } // expected-note {{in defaulted copy constructor for 'CCopyHolder' first required here}} ms-note {{in defaulted copy constructor for 'CCopyHolder' first required here}}
CCopyHolder *moveHolder(CCopyHolder &&_Holder) { return new CCopyHolder(static_cast<CCopyHolder &&>(_Holder)); } // expected-note {{in defaulted move constructor for 'CCopyHolder' first required here}} ms-note {{in defaulted move constructor for 'CCopyHolder' first required here}}
// Nor what constant evaluation constructs in the initializer of a `constexpr`
// variable, also a static one that is constant initialized, which never runs,
// but what a variable it initializes at run time constructs.
struct CEvaluated {
  constexpr CEvaluated(int = 0) {}
  constexpr virtual ~CEvaluated() {}
};
constexpr CEvaluated produceEvaluated() { return {}; }
constexpr int consumeEvaluated(CEvaluated *_pConstant) {
  delete _pConstant;
  return 1;
}
int consumeAtRunTime(CEvaluated *_pConstant);
[[malterlib::sized_construction]] CEvaluated *constructWithEvaluated() {
  constexpr int n = consumeEvaluated(new CEvaluated(produceEvaluated()));
  static constexpr int s_n = consumeEvaluated(new CEvaluated(produceEvaluated()));
  int nAtRunTime = consumeAtRunTime(new CEvaluated(produceEvaluated())); // expected-error {{cannot construct 'CEvaluated' from a value another function produces}} ms-error {{cannot construct 'CEvaluated' from a value another function produces}}
  return new CEvaluated(n + s_n + nAtRunTime);
}
// Also the result of a statement expression, which code generation emits into
// the object, `({ prepareStatement(); CStatement(1); })`, but not one another
// function produces there.
struct CStatement {
  CStatement(int);
  virtual ~CStatement();
};
void prepareStatement();
CStatement produceStatement();
[[malterlib::sized_construction]] CStatement *constructFromStatement(void *_pMemory) {
  new CStatement(({ prepareStatement(); CStatement(1); }));
  return new CStatement(({ prepareStatement(); produceStatement(); })); // expected-error {{cannot construct 'CStatement' from a value another function produces}} ms-error {{cannot construct 'CStatement' from a value another function produces}}
}
// Nor anything of a `consteval` function, which only constant evaluation runs.
[[malterlib::sized_construction]] consteval int constructAtCompileTime() {
  CEvaluated *pEvaluated = new CEvaluated(produceEvaluated());
  delete pEvaluated;
  return 1;
}
static_assert(constructAtCompileTime() == 1);
// Nor the initializer of a `const int` that is constant, which code
// generation emits as its value.
[[malterlib::sized_construction]] CEvaluated *constructWithConstInt() {
  const int n = consumeEvaluated(new CEvaluated(produceEvaluated()));
  return new CEvaluated(n);
}
// Nor the initializer of a virtual base in a constructor of an abstract
// class, which no construction runs, as the most derived class initializes
// it, but that of a class that isn't abstract.
struct CVirtualBase {
  CVirtualBase(Produced *);
};
struct CAbstract : virtual CVirtualBase {
  [[malterlib::sized_construction]] CAbstract() : CVirtualBase(new Produced(produce())) {}
  virtual ~CAbstract();
  virtual void f_Run() = 0;
};
struct CConcrete : virtual CVirtualBase {
  [[malterlib::sized_construction]] CConcrete() : CVirtualBase(new Produced(produce())) {} // expected-error {{cannot construct 'Produced' from a value another function produces}} ms-error {{cannot construct 'Produced' from a value another function produces}}
  virtual ~CConcrete();
};
// Also what a compound literal is made of, which code generation emits into
// the object, `(CStatement){1}`.
[[malterlib::sized_construction]] CStatement *constructFromLiteral() {
  return new CStatement((CStatement){1});
}
// Nor the operands of `__noop`, which doesn't evaluate them.
#ifdef MS
[[malterlib::sized_construction]] Produced *constructWithNoop() {
  __noop(new Produced(produce()));
  return new Produced;
}
#endif
// Nor the operand of an assumption, which is never evaluated.
[[malterlib::sized_construction]] Produced *constructWithAssumption() {
  __builtin_assume(new Produced(produce()) != nullptr); // expected-warning {{assumption is ignored because it contains (potential) side-effects}} ms-warning {{assumption is ignored because it contains (potential) side-effects}} disabled-warning {{assumption is ignored because it contains (potential) side-effects}}
#ifdef MS
  __assume(new Produced(produce()) != nullptr); // ms-warning {{assumption is ignored because it contains (potential) side-effects}}
#endif
  return new Produced;
}
