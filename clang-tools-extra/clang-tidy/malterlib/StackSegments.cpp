//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

// The context functions of ucontext.h are declared only for X/Open; the
// Darwin extensions stay visible beside them.
#if defined(__APPLE__)
#define _XOPEN_SOURCE 700
#define _DARWIN_C_SOURCE
#endif

#include "StackSegments.h"
#include "llvm/Support/ProgramStack.h"
#include <vector>

#if defined(_WIN32)
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <pthread.h>
#include <sys/mman.h>
#include <ucontext.h>
#endif

#if defined(__has_feature)
#if __has_feature(address_sanitizer)
#define MALTERLIB_STACK_SEGMENTS_ASAN 1
#endif
#endif
#if defined(__SANITIZE_ADDRESS__)
#define MALTERLIB_STACK_SEGMENTS_ASAN 1
#endif
#if defined(MALTERLIB_STACK_SEGMENTS_ASAN)
#include <sanitizer/common_interface_defs.h>
#endif

// The context functions are deprecated on Darwin, where they are what switches
// the stack of a thread without another thread.
#if defined(__GNUC__)
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif

namespace clang::tidy::malterlib {

namespace {
// What a walk can take between two checks of the room left, which the
// segment it runs on keeps free.
constexpr uintptr_t StackReserve = uintptr_t(1) << 20;
// The size of a new segment.
constexpr size_t SegmentSize = size_t(32) << 20;

// The lowest address of the stack segment the thread runs on, 0 before it is
// asked.
thread_local uintptr_t t_Low = 0;

#if !defined(_WIN32)
// The segments of the thread no walk runs on, kept for the next, which a walk
// at the end of a segment asks for once for each call it makes there.
struct CFreeSegments {
  std::vector<void *> Segments;
  ~CFreeSegments() {
    for (void *Segment : Segments)
      munmap(Segment, SegmentSize);
  }
};
thread_local CFreeSegments t_FreeSegments;
#endif

// Of every compiler LLVM is built with, also MSVC.
uintptr_t stackPointer() { return llvm::getStackPointer(); }

// The lowest address of the thread's own stack; of one that can't be told,
// as low as the reserve below where it is asked first.
uintptr_t threadStackLow() {
#if defined(_WIN32)
  ULONG_PTR Low = 0;
  ULONG_PTR High = 0;
  GetCurrentThreadStackLimits(&Low, &High);
  return static_cast<uintptr_t>(Low);
#elif defined(__APPLE__)
  pthread_t Self = pthread_self();
  return reinterpret_cast<uintptr_t>(pthread_get_stackaddr_np(Self)) -
         pthread_get_stacksize_np(Self);
#else
  pthread_attr_t Attributes;
  void *Address = nullptr;
  size_t Size = 0;
  if (pthread_getattr_np(pthread_self(), &Attributes) == 0) {
    pthread_attr_getstack(&Attributes, &Address, &Size);
    pthread_attr_destroy(&Attributes);
  }
  if (Address)
    return reinterpret_cast<uintptr_t>(Address);
  return stackPointer() - StackReserve;
#endif
}

// What runs on a segment, and where the stack it was started from is.
struct CSegmentRun {
  llvm::function_ref<void()> fRun;
#if defined(_WIN32)
  void *Back = nullptr;
#else
  // Each with room for the machine context Darwin keeps in it, which the
  // declaration seen leaves out where ucontext_t is declared without
  // X/Open before this.
  struct CContext {
    ucontext_t Context;
    char MachineContext[4096];
  };
  CContext Back;
  CContext Run;
#endif
#if defined(MALTERLIB_STACK_SEGMENTS_ASAN)
  void *FakeStack = nullptr;
  const void *BackBottom = nullptr;
  size_t BackSize = 0;
#endif
};

#if defined(_WIN32)
// The run a segment starts next, set just before the switch to it, on the
// same thread.
thread_local CSegmentRun *t_pStarting = nullptr;

// The fibers of the thread no walk runs on, kept for the next, as the
// segments of POSIX are, and whether the thread was made a fiber for them,
// which it is again a thread of when it ends.
struct CFiberPool {
  std::vector<void *> Fibers;
  bool bConverted = false;
  ~CFiberPool() {
    for (void *Fiber : Fibers)
      DeleteFiber(Fiber);
    if (bConverted)
      ConvertFiberToThread();
  }
};
thread_local CFiberPool t_FiberPool;

// A segment: each run it is switched to with, one after another, back to
// what switched to it after each.
void CALLBACK segmentEntry(void *) {
  const uintptr_t Low = stackPointer() - SegmentSize + StackReserve / 4;
  for (;;) {
    CSegmentRun &Run = *t_pStarting;
    t_pStarting = nullptr;
    t_Low = Low;
    Run.fRun();
    SwitchToFiber(Run.Back);
  }
}
#else
// The run a new segment starts, which makecontext can't pass as a pointer:
// it passes only ints, which a pointer doesn't fit in on every host. Set
// just before the switch to the segment, on the same thread.
thread_local CSegmentRun *t_pStarting = nullptr;

void segmentEntry() {
  CSegmentRun &Run = *t_pStarting;
  t_pStarting = nullptr;
#if defined(MALTERLIB_STACK_SEGMENTS_ASAN)
  __sanitizer_finish_switch_fiber(nullptr, &Run.BackBottom, &Run.BackSize);
#endif
  Run.fRun();
#if defined(MALTERLIB_STACK_SEGMENTS_ASAN)
  __sanitizer_start_switch_fiber(nullptr, Run.BackBottom, Run.BackSize);
#endif
}
#endif
} // namespace

bool stackNearlyExhausted() {
  if (!t_Low)
    t_Low = threadStackLow();
  const uintptr_t Here = stackPointer();
  return Here < t_Low || Here - t_Low < StackReserve;
}

void runOnNewStackSegment(llvm::function_ref<void()> fRun) {
  CSegmentRun Run;
  Run.fRun = fRun;
  const uintptr_t WasLow = t_Low;
#if defined(_WIN32)
  // The thread made a fiber once, kept so until it ends (CFiberPool).
  if (!IsThreadAFiber()) {
    if (!ConvertThreadToFiber(nullptr)) {
      fRun();
      return;
    }
    t_FiberPool.bConverted = true;
  }
  Run.Back = GetCurrentFiber();
  void *Fiber = nullptr;
  if (!t_FiberPool.Fibers.empty()) {
    Fiber = t_FiberPool.Fibers.back();
    t_FiberPool.Fibers.pop_back();
  } else {
    Fiber = CreateFiberEx(StackReserve, SegmentSize, 0, segmentEntry, nullptr);
    if (!Fiber) {
      fRun();
      return;
    }
  }
  t_pStarting = &Run;
  SwitchToFiber(Fiber);
  t_FiberPool.Fibers.push_back(Fiber);
#else
  void *Stack = nullptr;
  if (!t_FreeSegments.Segments.empty()) {
    Stack = t_FreeSegments.Segments.back();
    t_FreeSegments.Segments.pop_back();
  } else {
    Stack = mmap(nullptr, SegmentSize, PROT_READ | PROT_WRITE,
                 MAP_PRIVATE | MAP_ANON, -1, 0);
    if (Stack == MAP_FAILED) {
      fRun();
      return;
    }
  }
  getcontext(&Run.Run.Context);
  Run.Run.Context.uc_stack.ss_sp = Stack;
  Run.Run.Context.uc_stack.ss_size = SegmentSize;
  Run.Run.Context.uc_link = &Run.Back.Context;
  makecontext(&Run.Run.Context, &segmentEntry, 0);
  t_Low = reinterpret_cast<uintptr_t>(Stack);
  t_pStarting = &Run;
#if defined(MALTERLIB_STACK_SEGMENTS_ASAN)
  __sanitizer_start_switch_fiber(&Run.FakeStack, Stack, SegmentSize);
#endif
  swapcontext(&Run.Back.Context, &Run.Run.Context);
#if defined(MALTERLIB_STACK_SEGMENTS_ASAN)
  __sanitizer_finish_switch_fiber(Run.FakeStack, nullptr, nullptr);
#endif
  t_FreeSegments.Segments.push_back(Stack);
#endif
  t_Low = WasLow;
}

} // namespace clang::tidy::malterlib
