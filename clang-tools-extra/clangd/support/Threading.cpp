//===--- Threading.cpp - Abstractions for multithreading ------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "support/Threading.h"
#include "support/Trace.h"
#include "llvm/ADT/ScopeExit.h"
#include "llvm/Support/Threading.h"
#include "llvm/Support/thread.h"
#include <algorithm>
#include <atomic>
#include <cerrno>
#include <optional>
#include <thread>
#if defined(__linux__)
#include <sys/resource.h>
#include <unistd.h>
#endif
#ifdef __USE_POSIX
#include <pthread.h>
#elif defined(__APPLE__)
#include <sys/resource.h>
#elif defined(_WIN32)
#include <windows.h>
#endif

namespace clang {
namespace clangd {

void Notification::notify() {
  {
    std::lock_guard<std::mutex> Lock(Mu);
    Notified = true;
    // Broadcast with the lock held. This ensures that it's safe to destroy
    // a Notification after wait() returns, even from another thread.
    CV.notify_all();
  }
}

bool Notification::wait(Deadline D) const {
  std::unique_lock<std::mutex> Lock(Mu);
  return clangd::wait(Lock, CV, D, [&] { return Notified; });
}

Semaphore::Semaphore(std::size_t MaxLocks) : FreeSlots(MaxLocks) {}

bool Semaphore::try_lock() {
  std::unique_lock<std::mutex> Lock(Mutex);
  if (FreeSlots > 0) {
    --FreeSlots;
    return true;
  }
  return false;
}

void Semaphore::lock() {
  trace::Span Span("WaitForFreeSemaphoreSlot");
  // trace::Span can also acquire locks in ctor and dtor, we make sure it
  // happens when Semaphore's own lock is not held.
  {
    std::unique_lock<std::mutex> Lock(Mutex);
    SlotsChanged.wait(Lock, [&]() { return FreeSlots > 0; });
    --FreeSlots;
  }
}

void Semaphore::unlock() {
  std::unique_lock<std::mutex> Lock(Mutex);
  ++FreeSlots;
  Lock.unlock();

  SlotsChanged.notify_one();
}

namespace {
#if defined(__linux__)
// The nice value threads run at normally.
int normalNice() {
  static const int Nice = [] {
    errno = 0;
    int Nice = getpriority(PRIO_PROCESS, getpid());
    return Nice == -1 && errno ? 0 : Nice;
  }();
  return Nice;
}

// Low priority is a nice value 10 above the normal one, which gives a thread
// about a tenth of the CPU time of a normal one, like QOS_CLASS_UTILITY on
// macOS. llvm::ThreadPriority::Low is SCHED_IDLE instead, which runs only on
// otherwise idle CPUs, below even nice 19.
bool setThreadLow(uint64_t ThreadID, bool Low) {
  int Nice = Low ? std::min(normalNice() + 10, 19) : normalNice();
  return !setpriority(PRIO_PROCESS, ThreadID, Nice);
}
#endif

bool setCurrentThreadLow(bool Low) {
#if defined(__linux__)
  return setThreadLow(llvm::get_threadid(), Low);
#else
  return llvm::set_thread_priority(Low ? llvm::ThreadPriority::Low
                                       : llvm::ThreadPriority::Default) ==
         llvm::SetThreadPriorityResult::SUCCESS;
#endif
}
} // namespace

bool AdjustableThreadPriority::isSupported() {
  // Probe on a separate thread, as it may not be able to raise its priority.
  static const bool Supported = [] {
#if defined(__linux__)
    if (normalNice() >= 19)
      return false;
#endif
    bool Result = false;
    std::thread([&] {
      Result = setCurrentThreadLow(true) && setCurrentThreadLow(false);
    }).join();
    return Result;
  }();
  return Supported;
}

void AdjustableThreadPriority::setLow(bool Low) {
  std::lock_guard<std::mutex> Lock(Mu);
  if (WantLow == Low)
    return;
  WantLow = Low;
  if (Low || !Attached || !IsLow.value_or(false))
    return;
  // Raise the running task, as the thread only applies the requested priority
  // before its next one.
#if defined(__APPLE__)
  if (!Override)
    Override = pthread_override_qos_class_start_np(Thread, QOS_CLASS_DEFAULT,
                                                   /*relative_priority=*/0);
#elif defined(__linux__)
  // isSupported() checked that threads may lower their nice value again.
  if (setThreadLow(ThreadID, false))
    IsLow = false;
#endif
  // Windows only lets a thread leave background mode itself.
}

void AdjustableThreadPriority::attach() {
  std::lock_guard<std::mutex> Lock(Mu);
  assert(!Attached && "Already attached?");
#if defined(__APPLE__)
  Thread = pthread_self();
#elif defined(__linux__)
  ThreadID = llvm::get_threadid();
#endif
  Attached = true;
  // The thread may inherit the priority of the thread that created it.
  IsLow.reset();
}

bool AdjustableThreadPriority::apply() {
  std::lock_guard<std::mutex> Lock(Mu);
  assert(Attached && "Not attached?");
  bool Low = WantLow;
  if (IsLow != Low && setCurrentThreadLow(Low))
    IsLow = Low;
#if defined(__APPLE__)
  // The thread set its own priority, so the override is no longer needed.
  if (Override) {
    pthread_override_qos_class_end_np(Override);
    Override = nullptr;
  }
#endif
  return IsLow.value_or(false);
}

void AdjustableThreadPriority::detach() {
  std::lock_guard<std::mutex> Lock(Mu);
  assert(Attached && "Not attached?");
#if defined(__APPLE__)
  if (Override) {
    pthread_override_qos_class_end_np(Override);
    Override = nullptr;
  }
#endif
  Attached = false;
}

AsyncTaskRunner::~AsyncTaskRunner() { wait(); }

bool AsyncTaskRunner::wait(Deadline D) const {
  std::unique_lock<std::mutex> Lock(Mutex);
  return clangd::wait(Lock, TasksReachedZero, D,
                      [&] { return InFlightTasks == 0; });
}

void AsyncTaskRunner::runAsync(const llvm::Twine &Name,
                               llvm::unique_function<void()> Action) {
  {
    std::lock_guard<std::mutex> Lock(Mutex);
    ++InFlightTasks;
  }

  llvm::scope_exit CleanupTask([this]() {
    std::lock_guard<std::mutex> Lock(Mutex);
    int NewTasksCnt = --InFlightTasks;
    if (NewTasksCnt == 0) {
      // Note: we can't unlock here because we don't want the object to be
      // destroyed before we notify.
      TasksReachedZero.notify_one();
    }
  });

  auto Task = [Name = Name.str(), Action = std::move(Action),
               Cleanup = std::move(CleanupTask)]() mutable {
    llvm::set_thread_name(Name);
    Action();
    // Make sure function stored by ThreadFunc is destroyed before Cleanup runs.
    Action = nullptr;
  };

  // Ensure our worker threads have big enough stacks to run clang.
  llvm::thread Thread(
      /*clang::DesiredStackSize*/ std::optional<unsigned>(8 << 20),
      std::move(Task));
  Thread.detach();
}

Deadline timeoutSeconds(std::optional<double> Seconds) {
  using namespace std::chrono;
  if (!Seconds)
    return Deadline::infinity();
  return steady_clock::now() +
         duration_cast<steady_clock::duration>(duration<double>(*Seconds));
}

void wait(std::unique_lock<std::mutex> &Lock, std::condition_variable &CV,
          Deadline D) {
  if (D == Deadline::zero())
    return;
  if (D == Deadline::infinity())
    return CV.wait(Lock);
  CV.wait_until(Lock, D.time());
}

bool PeriodicThrottler::operator()() {
  Rep Now = Stopwatch::now().time_since_epoch().count();
  Rep OldNext = Next.load(std::memory_order_acquire);
  if (Now < OldNext)
    return false;
  // We're ready to run (but may be racing other threads).
  // Work out the updated target time, and run if we successfully bump it.
  Rep NewNext = Now + Period;
  return Next.compare_exchange_strong(OldNext, NewNext,
                                      std::memory_order_acq_rel);
}

} // namespace clangd
} // namespace clang
