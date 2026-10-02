.. title:: clang-tidy - malterlib-async-destroy

malterlib-async-destroy
=======================

Checks the uses of ``fg_AsyncDestroy``, which schedules the asynchronous
teardown of an actor or functor for when a coroutine scope ends, except in a
branch of an ``if constexpr`` that is discarded, or of an ``if`` whose
condition is a constant that never chooses it, which doesn't run:

- The awaiter of ``fg_AsyncDestroy`` is ``co_await``-ed where it is made,
  which schedules the cleanup, not kept in a local to await later, ``auto
  Awaiter = fg_AsyncDestroy(Cleanup); co_await Awaiter;``, where what runs or
  throws before the ``co_await`` leaves the cleanup unscheduled. Returning it, for the caller to await, is fine, converted to one
  that keeps a copy of the cleanup function, like ``CAsyncDestroyAwaiter``,
  but not as it is, also as the value of a future, ``co_return
  fg_AsyncDestroy(Cleanup)``, since it refers to the function it is given until it is
  awaited, also given through a local reference to it, also one that extends
  a temporary, ``auto &&fCleanup = [...]``, an unchanged local
  pointer to it, ``*pCleanup``, or moved,
  ``fg_Move(Cleanup)`` (use
  ``fg_AsyncDestroyByValue`` to return a lambda, also called through an
  unchanged local pointer to it, also through a local it is
  kept in, ``auto Awaiter = fg_AsyncDestroyByValue(...); return Awaiter;``).
- The result of the ``co_await`` is kept in a named variable, also as either
  value of a conditional that initializes it. The cleanup runs when the result
  is destroyed, so a temporary runs it at the end of the statement.
- The result is not moved elsewhere, also through a local pointer to it,
  also one pointed at it after it is made, ``pCleanup = &Cleanup``, also by a
  cast to an rvalue, or
  forwarded, swapped or exchanged, also as either value of a conditional,
  ``fg_Move(bChoose ? Cleanup : Other)``, or through a reference bound to one, where the cleanup can run after the locals
  it refers to are gone. ``co_await fg_Move(Cleanup)``, which runs the cleanup
  early, is fine, also with a cast, ``co_await static_cast<CGuard
  &&>(Cleanup)``.
- The cleanup function may capture by reference (what has static storage,
  ``[&Value = g_Value]``, also a local reference to it captured by reference,
  ``[&Value]`` of ``int &Value = g_Value``, lives as long as the program), but uses those references only
  before its first suspension, after which they are gone, unless what they
  refer to outlives the cleanup: in code that runs to completion (see
  :doc:`malterlib-runs-to-completion <runs-to-completion>`), the parameters of
  the coroutine, and the locals declared before a guard that is awaited
  early. So does a local pointer pointed into them, also through a reference
  to it, ``int *&pAlias = pValue``, and a lambda that captures them, called
  after, ``fRead()`` or ``fRead.operator()()``, also through a lambda it
  captures, ``[&fRead] { return fRead(); }``, and what a call uses that is
  evaluated before its arguments suspend, ``Value.f_Read(co_await
  fg_Work())``, also what a pointer given to it points to, ``fg_Read(&Value,
  co_await fg_Work())``, also cast, ``static_cast<int const *>(&Value)``, or
  made with ``std::addressof(Value)``, also of a constructor, ``CRead{Value, co_await
  fg_Work()}``, or through a pointer to a function.
  ``co_await ECoroutineFlag_CaptureExceptions`` does not suspend.
- Coroutines tear down actors and functors with ``fg_AsyncDestroy`` instead of
  calling ``f_Destroy`` from ``g_OnScopeExit``, also of a lambda kept in a
  local before, ``g_OnScopeExit / fg_Move(fCleanup)`` (not where it isn't
  evaluated, ``sizeof(Resource.f_Destroy())``, ``decltype`` or a
  requirement, ``requires { Resource.f_Destroy(); }``, nor in a lambda
  the cleanup makes but doesn't call).

.. code-block:: c++

  auto Cleanup = co_await fg_AsyncDestroy
    (
      [&Upload]() -> TCFuture<void>
      {
        co_await ECoroutineFlag_CaptureExceptions;
        auto ToDestroy = fg_Move(Upload);  // before the first suspension
        co_await fg_Move(ToDestroy).f_Destroy();
        co_return {};
      }
    )
  ;

The cleanup can only name what is already declared, and locals are destroyed
in reverse order, but a pointer that it captures by reference can be pointed
later at a local declared after the result, which is destroyed first: that
assignment, like ``pValue = &Local`` or ``pValue = Late + 1``, also in a
lambda that is called or that ``fg_CallSafe`` runs, also through a chain of
local references to the local, ``CValue &Second = First;``, also through
either value of a conditional, ``bChoose ? &Local : nullptr``, assigned
through a cast to a reference, ``static_cast<int *&>(pValue) = &Local``, or as
either of a conditional, ``(bFirst ? pValue : pOther) = &Local``, a copy of
another local pointer, ``pValue = pLate``, of what that one was last assigned,
or any of what it is given when that can't be told, a cast or
``std::addressof(Local)``, also what a getter whose body can be seen gives of
the data of a local or of what it owns, ``pValue = Late.f_Get()``, but not the
address of what a local reference is bound to before the result, ``pValue =
&Alias``, is reported, unless the cleanup has run
or been cancelled before it on every path, like with ``co_await
fg_Move(Cleanup)``, also by a cast to an rvalue reference,
``static_cast<decltype(Cleanup) &&>(Cleanup)``, also by either on each path,
like a join in one branch of an ``if`` and ``Cleanup.f_Clear()`` in the other,
also through a reference to the result. The pointer is
also followed through references to it, copies of other local pointers,
``fg_Exchange`` and swaps, ``fg_Swap(pValue, pLate)``, also through a local
reference bound to either of two locals, ``CValue &Alias = bChoose ? First :
Second``; a pointer the cleanup only assigns, ``pValue = nullptr``,
isn't used by it. A cleanup made by a factory lambda held in a local,
``fg_AsyncDestroy(fMakeCleanup())``, is the lambda it returns, also for what
it uses after its suspensions, also in a lambda it runs by ``std::invoke``, and the result through pointers to it, ``fg_Move(*pCleanup)``. The result moved through
references to it, also references to those, is reported like a move of it.

A template is checked as each of its instantiations makes it, and one that
isn't instantiated isn't checked. A cleanup whose control flow is too complex
to build, like through a long chain of default arguments that are
conditionals of calls that destroy temporaries, is reported, since what it
uses can't be checked.
