# Writes a translation unit with many overrides of one virtual method of an
# actor and of a class an actor calls, each with its body, and member
# functions of an actor that call them through a pointer: the overrides a
# call can run are told in the order of their places in the source.
import sys

Count = int(sys.argv[1])
print('#include "malterlib-concurrency.h"')
print('struct CMap { int *f_FindEqual(int) const; };')
print('TCFuture<int> fg_Work();')
print('struct COnResume {\n  ~COnResume();\n};')
print('struct CExceptionPointer {\n  CExceptionPointer();\n};')
print('template <typename tf_FCallback>\nTCFuture<COnResume> fg_OnResume(tf_FCallback &&_fCallback);')
print('struct CRefresher {\n  virtual void f_Refresh(int *&o_pValue) {}\n};')
for Index in range(Count):
    print('struct CRefresher%d : CRefresher {\n  void f_Refresh(int *&o_pValue) override { o_pValue = nullptr; }\n};'
          % Index)
print('struct CBaseActor : CActor {\n  virtual TCFuture<int> f_Run(int _Value);\n};')
for Index in range(Count):
    print('struct CActor%d : CBaseActor {\n  TCFuture<int> f_Run(int _Value) override { co_await fg_Work(); '
          'co_return _Value; }\n};' % Index)
print('''struct CApp : CActor {
  CMap m_Items;
  CRefresher *m_pRefresher;
  CBaseActor *m_pOther;
  TCFuture<int> f_Refreshed() {
    int *pValue = m_Items.f_FindEqual(0);
    auto Guard = co_await fg_OnResume([&]() -> CExceptionPointer {
      m_pRefresher->f_Refresh(pValue);
      return {};
    });
    co_await fg_Work();
    co_return *pValue;
  }
  TCFuture<int> f_Dispatched() {
    int Value = 0;
    co_return co_await m_pOther->f_Run(Value);
  }
};''')
