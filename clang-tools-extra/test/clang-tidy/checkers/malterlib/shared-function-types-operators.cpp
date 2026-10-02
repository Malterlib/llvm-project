// RUN: clang-tidy --checks='-*,malterlib-coroutine-actor-reference' --config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" %s -- -std=c++20 -I %S/Inputs 2>&1 | FileCheck %s

// The namespaces argument-dependent lookup looks in for the operators of a
// class are collected from each type of its template arguments once, also a
// function type that names another eight times, which would be looked at
// again through each path to it, a number of times exponential in its depth.

#include "malterlib-concurrency.h"

using C0 = int;
#define DLevel(N, P) using F##N = P (*)(P, P, P, P, P, P, P, P);
DLevel(1, C0) DLevel(2, F1) DLevel(3, F2) DLevel(4, F3) DLevel(5, F4) DLevel(6, F5) DLevel(7, F6) DLevel(8, F7) DLevel(9, F8) DLevel(10, F9)

template <class t_CType>
struct TCBuffer
{
	int *f_Get();
};

TCFuture<int> fg_Suspend();
struct CTestActor : CActor
{
	TCBuffer<F10> mp_Buffer;
	TCFuture<int> f_Test()
	{
		int *p = mp_Buffer.f_Get();
		co_await fg_Suspend();
		co_return *p;
	}
};

// CHECK: warning: {{.*}} [malterlib-coroutine-actor-reference]
