// RUN: %check_clang_tidy -std=c++23-or-later %s malterlib-thread-local-storage %t -- -config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" -- -I %S/Inputs

// Classes nested eight deep, each with sixteen members of the one before,
// made by default: the default member initializers of each class are
// collected once, not once for each way to them.

thread_local int t_Value;

struct CNest0 {
  int *m_pValue = &t_Value;
};
struct CNest1 {
  CNest0 m_0; CNest0 m_1; CNest0 m_2; CNest0 m_3; CNest0 m_4; CNest0 m_5; CNest0 m_6; CNest0 m_7; CNest0 m_8; CNest0 m_9; CNest0 m_10; CNest0 m_11; CNest0 m_12; CNest0 m_13; CNest0 m_14; CNest0 m_15;
};
struct CNest2 {
  CNest1 m_0; CNest1 m_1; CNest1 m_2; CNest1 m_3; CNest1 m_4; CNest1 m_5; CNest1 m_6; CNest1 m_7; CNest1 m_8; CNest1 m_9; CNest1 m_10; CNest1 m_11; CNest1 m_12; CNest1 m_13; CNest1 m_14; CNest1 m_15;
};
struct CNest3 {
  CNest2 m_0; CNest2 m_1; CNest2 m_2; CNest2 m_3; CNest2 m_4; CNest2 m_5; CNest2 m_6; CNest2 m_7; CNest2 m_8; CNest2 m_9; CNest2 m_10; CNest2 m_11; CNest2 m_12; CNest2 m_13; CNest2 m_14; CNest2 m_15;
};
struct CNest4 {
  CNest3 m_0; CNest3 m_1; CNest3 m_2; CNest3 m_3; CNest3 m_4; CNest3 m_5; CNest3 m_6; CNest3 m_7; CNest3 m_8; CNest3 m_9; CNest3 m_10; CNest3 m_11; CNest3 m_12; CNest3 m_13; CNest3 m_14; CNest3 m_15;
};
struct CNest5 {
  CNest4 m_0; CNest4 m_1; CNest4 m_2; CNest4 m_3; CNest4 m_4; CNest4 m_5; CNest4 m_6; CNest4 m_7; CNest4 m_8; CNest4 m_9; CNest4 m_10; CNest4 m_11; CNest4 m_12; CNest4 m_13; CNest4 m_14; CNest4 m_15;
};
struct CNest6 {
  CNest5 m_0; CNest5 m_1; CNest5 m_2; CNest5 m_3; CNest5 m_4; CNest5 m_5; CNest5 m_6; CNest5 m_7; CNest5 m_8; CNest5 m_9; CNest5 m_10; CNest5 m_11; CNest5 m_12; CNest5 m_13; CNest5 m_14; CNest5 m_15;
};
struct CNest7 {
  CNest6 m_0; CNest6 m_1; CNest6 m_2; CNest6 m_3; CNest6 m_4; CNest6 m_5; CNest6 m_6; CNest6 m_7; CNest6 m_8; CNest6 m_9; CNest6 m_10; CNest6 m_11; CNest6 m_12; CNest6 m_13; CNest6 m_14; CNest6 m_15;
};
struct CNest8 {
  CNest7 m_0; CNest7 m_1; CNest7 m_2; CNest7 m_3; CNest7 m_4; CNest7 m_5; CNest7 m_6; CNest7 m_7; CNest7 m_8; CNest7 m_9; CNest7 m_10; CNest7 m_11; CNest7 m_12; CNest7 m_13; CNest7 m_14; CNest7 m_15;
};

inline CNest8 *fg_MakeNest() { return new CNest8; }
// CHECK-MESSAGES: :[[@LINE-1]]:43: warning: a pointer to thread-local data is stored in the member 'm_pValue'
