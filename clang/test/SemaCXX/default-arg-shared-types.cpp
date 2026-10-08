// RUN: %clang_cc1 -std=c++20 -fsyntax-only -verify %s
// expected-no-diagnostics

// The scan of a default argument of an instantiation for immediate invocations
// and temporaries goes through each type in it once, also one that a type
// names twice, like the parameters and result of `F39 (*)(F39, F39)`, of
// the instantiated parameter or of a lambda in it, not once for each path to
// it, which is exponential in the depth of such types.

using F0 = int;
using F1 = F0 (*)(F0, F0);
using F2 = F1 (*)(F1, F1);
using F3 = F2 (*)(F2, F2);
using F4 = F3 (*)(F3, F3);
using F5 = F4 (*)(F4, F4);
using F6 = F5 (*)(F5, F5);
using F7 = F6 (*)(F6, F6);
using F8 = F7 (*)(F7, F7);
using F9 = F8 (*)(F8, F8);
using F10 = F9 (*)(F9, F9);
using F11 = F10 (*)(F10, F10);
using F12 = F11 (*)(F11, F11);
using F13 = F12 (*)(F12, F12);
using F14 = F13 (*)(F13, F13);
using F15 = F14 (*)(F14, F14);
using F16 = F15 (*)(F15, F15);
using F17 = F16 (*)(F16, F16);
using F18 = F17 (*)(F17, F17);
using F19 = F18 (*)(F18, F18);
using F20 = F19 (*)(F19, F19);
using F21 = F20 (*)(F20, F20);
using F22 = F21 (*)(F21, F21);
using F23 = F22 (*)(F22, F22);
using F24 = F23 (*)(F23, F23);
using F25 = F24 (*)(F24, F24);
using F26 = F25 (*)(F25, F25);
using F27 = F26 (*)(F26, F26);
using F28 = F27 (*)(F27, F27);
using F29 = F28 (*)(F28, F28);
using F30 = F29 (*)(F29, F29);
using F31 = F30 (*)(F30, F30);
using F32 = F31 (*)(F31, F31);
using F33 = F32 (*)(F32, F32);
using F34 = F33 (*)(F33, F33);
using F35 = F34 (*)(F34, F34);
using F36 = F35 (*)(F35, F35);
using F37 = F36 (*)(F36, F36);
using F38 = F37 (*)(F37, F37);
using F39 = F38 (*)(F38, F38);
using F40 = F39 (*)(F39, F39);

template <class T> void fg_Use(T = nullptr) {}
void fg_Test() { fg_Use<F40>(); }

template <class T> void fg_Lambda(int = [](T) { return 0; }(T{}));
void fg_TestLambda() { fg_Lambda<F40>(); }
