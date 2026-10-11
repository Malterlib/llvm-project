// RUN: %clang_cc1 -std=c++20 -fsyntax-only -verify %s
// RUN: %clang_cc1 -std=c++20 -fsyntax-only -verify -DREVERSED %s

// A chain of diamonds, each two classes with a virtual base of the one
// before, reaches each virtual base through each class of each diamond above
// it. Whether a class derives from the class of a protected member, and
// which friend of a class between the object's class and the member's class
// grants access to it, are found in time linear in the length of the chain,
// not exponential.

struct CMember {
protected:
  int m_Value; // #member
};

template <int N> struct CLevel;
template <int N, int Side> struct CBranch : virtual CLevel<N - 1> {};
#ifdef REVERSED
template <int N> struct CLevel : CBranch<N, 1>, CBranch<N, 0> {};
#else
template <int N> struct CLevel : CBranch<N, 0>, CBranch<N, 1> {};
#endif
template <> struct CLevel<0> : CMember {};

// A friend of the last class of the chain on the path to the member, declared
// before the chain is instantiated.
template <> struct CBranch<40, 1> : virtual CLevel<39> {
  friend int fg_Get(CLevel<40> &Value);
};

// A friend of the class of the object, which doesn't derive from the class
// of the member.
struct COther {
  friend struct CFriend;

protected:
  int m_Value;
};

struct CFriend : CLevel<40> {
  int f_Get(COther &Other) { return Other.m_Value; }
};

int fg_Get(CLevel<40> &Value) { return Value.m_Value; }

// No class on a path to the member befriends this one.
int fg_GetUnfriended(CLevel<40> &Value) {
  return Value.m_Value; // expected-error {{'m_Value' is a protected member of 'CMember'}}
  // expected-note@#member {{declared protected here}}
}
