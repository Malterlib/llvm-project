// RUN: %clang_cc1 -std=c++20 -fsyntax-only -verify %s
// RUN: %clang_cc1 -std=c++20 -fsyntax-only -verify -DVIRTUAL_DESTRUCTOR %s
// RUN: %clang_cc1 -std=c++20 -fsyntax-only -verify -DVIRTUAL_FUNCTION %s
// RUN: %clang_cc1 -std=c++20 -fsyntax-only -verify -Woverloaded-virtual %s
// expected-no-diagnostics

// A chain of diamonds, each two classes with a virtual base of the one
// before, reaches each virtual base through each class of each diamond above
// it. The final overriders of the destructor or a function of the first, the
// primary virtual bases of the layout, the method a call through the last
// one calls, the virtual members an object of it uses, and its conversions,
// are found in time linear in the length of the chain, not exponential.

template <int N> struct CLevel;
template <int N, int Side> struct CBranch : virtual CLevel<N - 1> {};
template <int N> struct CLevel : CBranch<N, 0>, CBranch<N, 1> {};
#if defined(VIRTUAL_DESTRUCTOR)
template <> struct CLevel<0> {
  virtual ~CLevel() = default;
};
#elif defined(VIRTUAL_FUNCTION)
struct CRoot {
  virtual void f();
};
template <> struct CLevel<0> : CRoot {};
void fg_Call(CLevel<40> &Value) { Value.f(); }
#else
template <> struct CLevel<0> {};
#endif

static_assert(sizeof(CLevel<40>) > 0);

CLevel<40> g_Object;

static_assert(!__is_convertible_to(CLevel<40> &, int));
struct CBox {
  CBox(CLevel<40> const &);
};
void fg_Box(CLevel<40> const &Value) { CBox Box(Value); }

// Also the conversions of a chain whose classes convert to types of their own,
// which hide none of those of their bases.
template <int N> struct CTag {};
template <int N> struct CConvertible;
template <int N> struct CLeft : virtual CConvertible<N - 1> {
  operator CTag<2 * N>() const;
};
template <int N> struct CRight : virtual CConvertible<N - 1> {
  operator CTag<2 * N + 1>() const;
};
template <int N> struct CConvertible : CLeft<N>, CRight<N> {};
template <> struct CConvertible<0> {
  operator int() const;
};
int fg_Convert(CConvertible<40> const &Value) { return Value; }

// And of one whose virtual base converts to many types, which the classes of
// each path hide different ones of.
template <int N> struct CTagged {};
template <int N> struct CConversion {
  operator CTagged<N>();
};
template <int N> struct CAll : CAll<N - 1>, CConversion<N> {};
template <> struct CAll<0> : CConversion<0> {};
template <int N> struct CHiding;
template <int N> struct CHidingLeft : virtual CHiding<N - 1> {
  operator CTagged<N>();
};
template <int N> struct CHidingRight : virtual CHiding<N - 1> {};
template <int N> struct CHiding : CHidingLeft<N>, CHidingRight<N> {};
template <> struct CHiding<0> : CAll<40> {};
CTagged<1> fg_Hidden(CHiding<40> &Value) { return Value; }

// And whether a class is a base of the last one, which a qualified call of a
// set of static and non-static overloads asks of each class of the chain.
struct COverloads {
  static void f_Run();
  void f_Run(int);
};
struct CCaller : CLevel<40> {
  void f_Call() { COverloads::f_Run(); }
};

// And so does the deduction of the class template arguments of a braced
// initialization from the last one, which asks it of each class of the chain.
template <class T> struct CDeduced {
  CDeduced(T const &);
};
void fg_Deduce(CLevel<40> const &Value) { CDeduced Deduced{Value}; }

// And the methods a method of the last one overrides that override none, which
// -Woverloaded-virtual looks for, through those of each class of a chain whose
// classes each override it.
template <int N> struct COverriding;
template <int N> struct COverridingLeft : virtual COverriding<N - 1> {
  void f_Run() override;
};
template <int N> struct COverridingRight : virtual COverriding<N - 1> {
  void f_Run() override;
};
template <int N>
struct COverriding : COverridingLeft<N>, COverridingRight<N> {
  void f_Run() override;
};
template <> struct COverriding<0> {
  virtual void f_Run();
};
static_assert(sizeof(COverriding<40>) > 0);
