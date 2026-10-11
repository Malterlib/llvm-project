// RUN: %clang_cc1 -std=c++20 -fsyntax-only -verify -Woverloaded-virtual %s

// A chain of diamonds, each two classes with a virtual base of the one
// before, which each override a method of the first, reaches it through each
// class of each diamond above it. A method of a class derived from the last
// one that hides it, rather than overriding it, is reported, and the search of
// -Woverloaded-virtual for the methods it overrides goes through each method
// once, in time linear in the length of the chain, not exponential.

template <int N> struct CLevel;
template <int N> struct CLeft : virtual CLevel<N - 1> {
  void f_Run() override;
};
template <int N> struct CRight : virtual CLevel<N - 1> {
  void f_Run() override;
};
template <int N> struct CLevel : CLeft<N>, CRight<N> {
  void f_Run() override; // expected-note {{hidden overloaded virtual function 'CLevel<40>::f_Run' declared here}}
};
template <> struct CLevel<0> {
  virtual void f_Run();
};

struct CHiding : CLevel<40> {
  void f_Run(int); // expected-warning {{'CHiding::f_Run' hides overloaded virtual function}}
};

// But not one that a using declaration brings in.
struct CUsing : CLevel<40> {
  using CLevel<40>::f_Run;
  void f_Run(int);
};
