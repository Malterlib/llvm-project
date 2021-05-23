//==-- llvm/Support/ThreadPool.cpp - A ThreadPool implementation -*- C++ -*-==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements a crude C++11 based thread pool.
//
//===----------------------------------------------------------------------===//

#include <cstddef>

#if _LIBCPP_VERSION >= 1000
#   include <__threading_support>
    _LIBCPP_BEGIN_NAMESPACE_STD
    static inline size_t __libcpp_thread_setstacksize(size_t __s = 0)
    {
        if (__s < PTHREAD_STACK_MIN && __s)
            __s = PTHREAD_STACK_MIN;

        thread_local static size_t __s_ = 0;
        size_t __r = __s_;
        __s_ = __s;
        return __r;
    }
    static inline int __libcpp_thread_create_ex(__libcpp_thread_t *__t, void *(*__func)(void *), void *__arg)
    {
        size_t __s = __libcpp_thread_setstacksize();

        pthread_attr_t __a;
        ::pthread_attr_init(&__a);
        if (__s) ::pthread_attr_setstacksize(&__a, __s);
        int __r = ::pthread_create(__t, &__a, __func, __arg);
        ::pthread_attr_destroy(&__a);
        return __r;
    }
    _LIBCPP_END_NAMESPACE_STD
#   if defined(_LIBCPP_THREAD)
#       error #include <thread> is already included.
#   endif
#   define __libcpp_thread_create __libcpp_thread_create_ex
#   include <thread>
#   undef __libcpp_thread_create
    _LIBCPP_BEGIN_NAMESPACE_STD
    template<class _Fp, class ..._Args>
    _LIBCPP_METHOD_TEMPLATE_IMPLICIT_INSTANTIATION_VIS
    thread stacking_thread(size_t __s, _Fp&& __f, _Args&&... __args)
    {
        __libcpp_thread_setstacksize(__s);
        return thread(__f, __args...);
    }
    _LIBCPP_END_NAMESPACE_STD
#endif

#if __GLIBCXX__
#   include <limits.h>
#   include <pthread.h>
#   if defined(_GLIBCXX_THREAD)
#       error #include <thread> is already included.
#   endif
#   define _M_start_thread _M_start_thread_ex
#   include <thread>
#   undef _M_start_thread
    namespace std {
    _GLIBCXX_BEGIN_NAMESPACE_VERSION
    static void* _M_execute_native_thread_routine(void* __p)
    {
        thread::_State_ptr __t{ static_cast<thread::_State*>(__p) };
        __t->_M_run();
        return nullptr;
    }
    static inline size_t _M_thread_setstacksize(size_t __s = 0)
    {
        if (__s < PTHREAD_STACK_MIN && __s)
            __s = PTHREAD_STACK_MIN;

        thread_local static size_t __s_ = 0;
        size_t __r = __s_;
        __s_ = __s;
        return __r;
    }
    void thread::_M_start_thread_ex(_State_ptr state, void (*)())
    {
        size_t __s = _M_thread_setstacksize();

        pthread_attr_t __a;
        ::pthread_attr_init(&__a);
        if (__s) ::pthread_attr_setstacksize(&__a, __s);
        const int err = pthread_create(&_M_id._M_thread, &__a, &_M_execute_native_thread_routine, state.get());
        ::pthread_attr_destroy(&__a);
        if (err)
            __throw_system_error(err);
        state.release();
    }
    template<class _Fp, class ..._Args>
    thread stacking_thread(size_t __s, _Fp&& __f, _Args&&... __args)
    {
        _M_thread_setstacksize(__s);
        return thread(__f, __args...);
    }
    _GLIBCXX_END_NAMESPACE_VERSION
    }
#endif

#if _MSVC_STL_UPDATE
#   include <process.h>
    _STD_BEGIN
    static inline unsigned _thread_setstacksize(unsigned __s = 0)
    {
        if (__s < 65536 && __s)
            __s = 65536;

        thread_local static unsigned __s_ = 0;
        unsigned __r = __s_;
        __s_ = __s;
        return __r;
    }
    _STD_END
#   if defined(_THREAD_)
#       error #include <thread> is already included.
#   endif
#   define _beginthreadex(a,b,c,d,e,f) _beginthreadex(a,_thread_setstacksize(),c,d,e,f)
#   include <thread>
#   undef _beginthreadex
    _STD_BEGIN
    template<class _Fp, class ..._Args>
    thread stacking_thread(unsigned __s, _Fp&& __f, _Args&&... __args)
    {
        _thread_setstacksize(__s);
        return thread(__f, __args...);
    }
    _STD_END
#endif


#include "llvm/Support/ThreadPool.h"

#include "llvm/Config/llvm-config.h"
#include "llvm/Support/Threading.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

#if LLVM_ENABLE_THREADS

ThreadPool::ThreadPool(ThreadPoolStrategy S)
    : ThreadCount(S.compute_thread_count()) {
  // Create ThreadCount threads that will loop forever, wait on QueueCondition
  // for tasks to be queued or the Pool to be destroyed.
  Threads.reserve(ThreadCount);
  for (unsigned ThreadID = 0; ThreadID < ThreadCount; ++ThreadID) {
    Threads.emplace_back(std::stacking_thread(8*1024*1024, [S, ThreadID, this] {
      S.apply_thread_strategy(ThreadID);
      while (true) {
        PackagedTaskTy Task;
        {
          std::unique_lock<std::mutex> LockGuard(QueueLock);
          // Wait for tasks to be pushed in the queue
          QueueCondition.wait(LockGuard,
                              [&] { return !EnableFlag || !Tasks.empty(); });
          // Exit condition
          if (!EnableFlag && Tasks.empty())
            return;
          // Yeah, we have a task, grab it and release the lock on the queue

          // We first need to signal that we are active before popping the queue
          // in order for wait() to properly detect that even if the queue is
          // empty, there is still a task in flight.
          ++ActiveThreads;
          Task = std::move(Tasks.front());
          Tasks.pop();
        }
        // Run the task we just grabbed
        Task();

        bool Notify;
        {
          // Adjust `ActiveThreads`, in case someone waits on ThreadPool::wait()
          std::lock_guard<std::mutex> LockGuard(QueueLock);
          --ActiveThreads;
          Notify = workCompletedUnlocked();
        }
        // Notify task completion if this is the last active thread, in case
        // someone waits on ThreadPool::wait().
        if (Notify)
          CompletionCondition.notify_all();
      }
    }));
  }
}

void ThreadPool::wait() {
  // Wait for all threads to complete and the queue to be empty
  std::unique_lock<std::mutex> LockGuard(QueueLock);
  CompletionCondition.wait(LockGuard, [&] { return workCompletedUnlocked(); });
}

std::shared_future<void> ThreadPool::asyncImpl(TaskTy Task) {
  /// Wrap the Task in a packaged_task to return a future object.
  PackagedTaskTy PackagedTask(std::move(Task));
  auto Future = PackagedTask.get_future();
  {
    // Lock the queue and push the new task
    std::unique_lock<std::mutex> LockGuard(QueueLock);

    // Don't allow enqueueing after disabling the pool
    assert(EnableFlag && "Queuing a thread during ThreadPool destruction");

    Tasks.push(std::move(PackagedTask));
  }
  QueueCondition.notify_one();
  return Future.share();
}

// The destructor joins all threads, waiting for completion.
ThreadPool::~ThreadPool() {
  {
    std::unique_lock<std::mutex> LockGuard(QueueLock);
    EnableFlag = false;
  }
  QueueCondition.notify_all();
  for (auto &Worker : Threads)
    Worker.join();
}

#else // LLVM_ENABLE_THREADS Disabled

// No threads are launched, issue a warning if ThreadCount is not 0
ThreadPool::ThreadPool(ThreadPoolStrategy S)
    : ThreadCount(S.compute_thread_count()) {
  if (ThreadCount != 1) {
    errs() << "Warning: request a ThreadPool with " << ThreadCount
           << " threads, but LLVM_ENABLE_THREADS has been turned off\n";
  }
}

void ThreadPool::wait() {
  // Sequential implementation running the tasks
  while (!Tasks.empty()) {
    auto Task = std::move(Tasks.front());
    Tasks.pop();
    Task();
  }
}

std::shared_future<void> ThreadPool::asyncImpl(TaskTy Task) {
  // Get a Future with launch::deferred execution using std::async
  auto Future = std::async(std::launch::deferred, std::move(Task)).share();
  // Wrap the future so that both ThreadPool::wait() can operate and the
  // returned future can be sync'ed on.
  PackagedTaskTy PackagedTask([Future]() { Future.get(); });
  Tasks.push(std::move(PackagedTask));
  return Future;
}

ThreadPool::~ThreadPool() { wait(); }

#endif
