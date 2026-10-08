// RUN: %clang_cc1 -fsyntax-only -std=c++20 -fopenmp -fopenmp-version=52 -fopenmp-is-target-device -verify=device %s
// RUN: %clang_cc1 -fsyntax-only -std=c++20 -fopenmp -fopenmp-version=52 -verify=host %s

// host-no-diagnostics

// The default argument of each function in this chain uses the one before it
// twice, so the deferred diagnostics of the device functions that use the last
// one reach the first 2^40 times through the uses: each default argument is
// visited once for each function that uses it, which diagnoses the host
// function the first one calls once for each of them.
void host_only();
#pragma omp declare target enter(host_only) device_type(host)
// device-note@-1 2 {{marked as 'device_type(host)' here}}
int h(int = (host_only(), 0)); // device-error 2 {{function with 'device_type(host)' is not available on device}}
int f0(int = h());
int f1(int = f0() + f0());
int f2(int = f1() + f1());
int f3(int = f2() + f2());
int f4(int = f3() + f3());
int f5(int = f4() + f4());
int f6(int = f5() + f5());
int f7(int = f6() + f6());
int f8(int = f7() + f7());
int f9(int = f8() + f8());
int f10(int = f9() + f9());
int f11(int = f10() + f10());
int f12(int = f11() + f11());
int f13(int = f12() + f12());
int f14(int = f13() + f13());
int f15(int = f14() + f14());
int f16(int = f15() + f15());
int f17(int = f16() + f16());
int f18(int = f17() + f17());
int f19(int = f18() + f18());
int f20(int = f19() + f19());
int f21(int = f20() + f20());
int f22(int = f21() + f21());
int f23(int = f22() + f22());
int f24(int = f23() + f23());
int f25(int = f24() + f24());
int f26(int = f25() + f25());
int f27(int = f26() + f26());
int f28(int = f27() + f27());
int f29(int = f28() + f28());
int f30(int = f29() + f29());
int f31(int = f30() + f30());
int f32(int = f31() + f31());
int f33(int = f32() + f32());
int f34(int = f33() + f33());
int f35(int = f34() + f34());
int f36(int = f35() + f35());
int f37(int = f36() + f36());
int f38(int = f37() + f37());
int f39(int = f38() + f38());
int f40(int = f39() + f39());

#pragma omp begin declare target
void device1() { f40(); }
void device2() { f40(); f40(); }
#pragma omp end declare target

void host() { f40(); }
