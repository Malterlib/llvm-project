// RUN: %check_clang_tidy -std=c++23-or-later %s malterlib-thread-local-storage %t -- -config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" -- -I %S/Inputs

// Closures in default arguments whose bodies call functions with more of them,
// each body checked once, not once for each way to it.

thread_local int t_Value;
int *g_pValue;

inline int fg_Level0() { return 0; }
inline int fg_Level1(int _Value = [] { return fg_Level0(); }() + [] { return fg_Level0(); }()) { return _Value; }
inline int fg_Level2(int _Value = [] { return fg_Level1(); }() + [] { return fg_Level1(); }()) { return _Value; }
inline int fg_Level3(int _Value = [] { return fg_Level2(); }() + [] { return fg_Level2(); }()) { return _Value; }
inline int fg_Level4(int _Value = [] { return fg_Level3(); }() + [] { return fg_Level3(); }()) { return _Value; }
inline int fg_Level5(int _Value = [] { return fg_Level4(); }() + [] { return fg_Level4(); }()) { return _Value; }
inline int fg_Level6(int _Value = [] { return fg_Level5(); }() + [] { return fg_Level5(); }()) { return _Value; }
inline int fg_Level7(int _Value = [] { return fg_Level6(); }() + [] { return fg_Level6(); }()) { return _Value; }
inline int fg_Level8(int _Value = [] { return fg_Level7(); }() + [] { return fg_Level7(); }()) { return _Value; }
inline int fg_Level9(int _Value = [] { return fg_Level8(); }() + [] { return fg_Level8(); }()) { return _Value; }
inline int fg_Level10(int _Value = [] { return fg_Level9(); }() + [] { return fg_Level9(); }()) { return _Value; }
inline int fg_Level11(int _Value = [] { return fg_Level10(); }() + [] { return fg_Level10(); }()) { return _Value; }
inline int fg_Level12(int _Value = [] { return fg_Level11(); }() + [] { return fg_Level11(); }()) { return _Value; }
inline int fg_Level13(int _Value = [] { return fg_Level12(); }() + [] { return fg_Level12(); }()) { return _Value; }
inline int fg_Level14(int _Value = [] { return fg_Level13(); }() + [] { return fg_Level13(); }()) { return _Value; }
inline int fg_Level15(int _Value = [] { return fg_Level14(); }() + [] { return fg_Level14(); }()) { return _Value; }
inline int fg_Level16(int _Value = [] { return fg_Level15(); }() + [] { return fg_Level15(); }()) { return _Value; }
inline int fg_Level17(int _Value = [] { return fg_Level16(); }() + [] { return fg_Level16(); }()) { return _Value; }
inline int fg_Level18(int _Value = [] { return fg_Level17(); }() + [] { return fg_Level17(); }()) { return _Value; }
inline int fg_Level19(int _Value = [] { return fg_Level18(); }() + [] { return fg_Level18(); }()) { return _Value; }
inline int fg_Level20(int _Value = [] { return fg_Level19(); }() + [] { return fg_Level19(); }()) { return _Value; }
inline int fg_Level21(int _Value = [] { return fg_Level20(); }() + [] { return fg_Level20(); }()) { return _Value; }
inline int fg_Level22(int _Value = [] { return fg_Level21(); }() + [] { return fg_Level21(); }()) { return _Value; }
inline int fg_Level23(int _Value = [] { return fg_Level22(); }() + [] { return fg_Level22(); }()) { return _Value; }
inline int fg_Level24(int _Value = [] { return fg_Level23(); }() + [] { return fg_Level23(); }()) { return _Value; }
inline int fg_Top() {
  g_pValue = &t_Value;
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: a pointer to thread-local data is stored in the variable 'g_pValue'{{.*}}
  return fg_Level24();
}
