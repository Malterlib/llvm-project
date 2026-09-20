// RUN: %clang_cc1 -std=c++20 -triple x86_64-linux-gnu -fmalterlib-sized-destructors -fsyntax-only -verify %s
// RUN: %clang_cc1 -std=c++20 -triple x86_64-linux-gnu -fsyntax-only -verify=disabled -DDISABLED %s
// The Microsoft ABI selects the destructor's mode with its flags argument, not
// with bit 0 of 'this', so packed classes are fine there.
// RUN: %clang_cc1 -std=c++20 -triple x86_64-pc-windows-msvc -fmalterlib-sized-destructors -fsyntax-only -verify=ms -DMS %s

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
