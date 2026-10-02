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
- The cleanup function may capture by reference (what is alive while it
  runs, as the model of :doc:`malterlib-coroutine-reference-capture
  <coroutine-reference-capture>` tells, lives as long as it needs: what has
  static storage, ``[&Value = g_Value]``, also a local reference to it
  captured by reference, ``[&Value]`` of ``int &Value = g_Value``, also
  through an unchanged pointer to it or a getter that gives it, ``[&Config =
  fg_Config()]`` of ``return s_Config;``, what owners own, another object
  and what a parameter refers to where the cleanup holds a keeper of it by
  type, in a capture, a parameter or a local of its body in scope at the
  use, declared before it in a scope still open there, also of the code around a cleanup that isn't a coroutine, out
  to the nearest coroutine, of an anchor of the borrow, every object type on
  its derivation, asked at each use it would report, ``[&Result, pJob]`` of ``int
  &Result = pJob->m_Result``, ``[&Job] { auto pKeep =
  Job.f_GetSharedPointer(); co_await ...; Job.m_Result = 0; }``, a keeper
  of what an owner owns only when it owns the same object where it is taken
  (:doc:`coroutine-reference-capture`) (an owner released or reassigned
  between the borrow and the capture where it isn't in a local slot, a
  keeper that owns a different object of a fitting type where the
  provenance can't tell, aren't reported), and the
  object of an actor in a cleanup made in one of its member functions), but
  uses those references only
  before its first suspension, after which they are gone, unless what they
  refer to outlives the cleanup: in code that runs to completion (see
  :doc:`malterlib-runs-to-completion <runs-to-completion>`), the parameters of
  the coroutine, and the locals declared before a guard that is awaited
  early, by what each reference designates and the rule of what lives until
  a join that reference capture asks too. So does a local pointer pointed into them, also through a reference
  to it, ``int *&pAlias = pValue``, and a lambda that captures them, called
  after, ``fRead()`` or ``fRead.operator()()``, also through a lambda it
  captures, ``[&fRead] { return fRead(); }``, which uses them where its body
  does, in the context there, its own keepers too, ``auto pKeep =
  fg_Lock(pWeakJob);`` before ``Job.m_Result``, and what a call uses that is
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

.. example-before
   struct CUpload
   {
     TCFuture<void> f_Destroy();
   };
   TCFuture<void> fg_Upload()
   {
     CUpload Upload;

.. example-after
     co_return {};
   }

.. code-block:: c++

  auto Cleanup = co_await fg_AsyncDestroy // fine
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
in reverse order, but a pointer that it captures by reference, and whose
pointee it uses when it runs, can be pointed later at a local declared after
the result, which is destroyed first: where the definition that does that is
made, an assignment, ``pValue = &Local`` or ``pValue = Late + 1``, also
through a reference to the pointer, a cast to a reference,
``static_cast<int *&>(pValue) = &Local``, or either of a conditional,
``(bFirst ? pValue : pOther) = &Local``, a swap, ``fg_Swap(pValue, pLate)``,
``fg_Exchange``, a setter whose body can be seen, also one that gives the
address of a member of what it is given, ``o_pValue = &_Value.m_Value``, or
through a local pointer made with it, and an assignment in a lambda that is
called or that ``fg_CallSafe`` runs. The pointer is each slot a capture by
reference designates, also through local references to it, ``int *&pAlias =
pValue``, also bound to either of two, ``int *&pAlias = bChoose ? p : q``; one
that the cleanup only assigns, ``pValue = nullptr``, or reads only after it
assigns it on each way, ``pValue = &g_Value; co_return *pValue;``, or reads
where it isn't evaluated, isn't used by it. What the definitions give is told
by the slot analysis that every check asks, whatever path or loop they take,
and where it points by provenance, the one model of where storage lives: a
local or a parameter held by value, also a temporary a local reference
extends, as that local, or what a local owner owns, the owner, past local
references, copies of local pointers, conditionals, casts,
``std::addressof(Local)``, and what a getter whose body can be seen gives of
a local, however long the chain of copies is. What matters is what the
pointer holds where the cleanup can run: where the guard ends, and where the
frame can end before that, a suspension, where the coroutine can be
destroyed (not an await that doesn't suspend, ``co_await
g_CaptureExceptions``, whose calls are told as calls), and what can throw, a
``throw``, an allocation, or a call or a construction whose exception
specification doesn't say it can't, also of the function type a call
through a pointer, a reference or a pointer to a member function names,
``void (*_fCallback)() noexcept``, each of which destroys the later local
first. A definition is reported when it can
reach such a place before the pointer is pointed at what outlives the guard
again, ``pValue = &Late; pValue = &Early;`` with nothing between isn't, and
before the guard is run or cancelled there, ``co_await fg_Move(Cleanup)``,
also by a cast to an rvalue reference, ``static_cast<decltype(Cleanup)
&&>(Cleanup)``, or ``Cleanup.f_Clear()``, also through a reference to the
result, also in a local closure that is called, each way through whose body
does that, ``auto fCommit = [&Cleanup] { Cleanup.f_Clear(); }; fCommit();``;
and when it is made where the guard isn't run or cancelled on each way there,
like a join in one branch of an ``if`` and ``Cleanup.f_Clear()`` in the
other. A definition that is statically dead in the instantiation doesn't run.
What can't be told isn't followed, nor a pointer member stored in what it is
given, ``pJob = pAlias.f_Get()`` of a ``TCPointer``, whose destruction doesn't
destroy what it points to, nor what an owner owns that the cleanup holds a
fitting owner of by value (Rule K, as for its captures), ``pValue =
pLate.f_Get();`` of ``auto pLate = pKeep;`` with ``[&pValue, pKeep]``, nor the
address of what a local reference is bound to before the result, ``pValue =
&Alias``. A cleanup
made by a factory lambda held in a local, ``fg_AsyncDestroy(fMakeCleanup())``,
is the lambda it returns, also for what it uses after its suspensions, also in
a lambda it runs by ``std::invoke``, and the result through pointers to it,
``fg_Move(*pCleanup)``. The result moved through references to it, also
references to those, is reported like a move of it.

A template is checked as each of its instantiations makes it, and one that
isn't instantiated isn't checked; a generic cleanup, like ``[&Value](this auto
const &_Self) -> TCFuture<void> { ... }``, is checked as each body its call
operator is instantiated with, never as written. A cleanup whose control flow is too complex
to build, like through a long chain of default arguments that are
conditionals of calls that destroy temporaries, is reported, since what it
uses can't be checked.

Nothing is reported in a translation unit with an error that stops it from
compiling: its control flow isn't built, and what the errors leave can't be
relied on.

.. example-check

  struct CTarget
  {
    TCFuture<void> f_Destroy();
    int m_Value = 0;
  };
  TCFuture<void> fg_Work();
  int g_Value = 0;
  TCFuture<void> fg_Kept()
  {
    CTarget Target;
    auto Awaiter = fg_AsyncDestroy // warning: the awaiter is kept to await later
      (
        [&Target]() -> TCFuture<void>
        {
          auto ToDestroy = fg_Move(Target);
          co_await ToDestroy.f_Destroy();
          co_return {};
        }
      )
    ;
    auto Cleanup = co_await fg_Move(Awaiter);
    co_return {};
  }
  TCFuture<void> fg_Unnamed()
  {
    CTarget Target;
    co_await fg_AsyncDestroy // warning: the result isn't kept in a named variable
      (
        [&Target]() -> TCFuture<void>
        {
          auto ToDestroy = fg_Move(Target);
          co_await ToDestroy.f_Destroy();
          co_return {};
        }
      )
    ;
    co_return {};
  }
  TCFuture<void> fg_Moved()
  {
    CTarget Target;
    auto Cleanup = co_await fg_AsyncDestroy
      (
        [&Target]() -> TCFuture<void>
        {
          auto ToDestroy = fg_Move(Target);
          co_await ToDestroy.f_Destroy();
          co_return {};
        }
      )
    ;
    auto Other = fg_Move(Cleanup); // warning: the result is moved elsewhere
    co_return {};
  }
  TCFuture<void> fg_RunEarly()
  {
    CTarget Target;
    auto Cleanup = co_await fg_AsyncDestroy // fine: copied before the first suspension, run early
      (
        [&Target]() -> TCFuture<void>
        {
          co_await ECoroutineFlag_CaptureExceptions;
          auto ToDestroy = fg_Move(Target);
          co_await ToDestroy.f_Destroy();
          co_return {};
        }
      )
    ;
    co_await fg_Move(Cleanup);
    co_return {};
  }
  TCFuture<void> fg_AfterSuspension()
  {
    CTarget Target;
    auto Cleanup = co_await fg_AsyncDestroy
      (
        [&Target]() -> TCFuture<void>
        {
          co_await fg_Work();
          co_await Target.f_Destroy(); // warning: used after the first suspension
          co_return {};
        }
      )
    ;
    co_return {};
  }
  TCFuture<void> fg_Static()
  {
    auto Cleanup = co_await fg_AsyncDestroy
      (
        [&Value = g_Value]() -> TCFuture<void>
        {
          co_await fg_Work();
          Value = 0; // fine: what has static storage
          co_return {};
        }
      )
    ;
    co_return {};
  }
  TCFuture<void> fg_PointedLater()
  {
    int Early = 0;
    int *pValue = &Early;
    auto Cleanup = co_await fg_AsyncDestroy
      (
        [&pValue]() -> TCFuture<void>
        {
          int Value = *pValue;
          co_return {};
        }
      )
    ;
    int Late = 0;
    pValue = &Late; // warning: pointed at a local declared after the result
    co_await fg_Work();
    co_return {};
  }
  TCFuture<void> fg_PointedBack()
  {
    int Early = 0;
    int *pValue = &Early;
    auto Cleanup = co_await fg_AsyncDestroy
      (
        [&pValue]() -> TCFuture<void>
        {
          int Value = *pValue;
          co_return {};
        }
      )
    ;
    int Late = 0;
    pValue = &Late; // fine: pointed back before anything can end the frame
    pValue = &Early;
    co_await fg_Work();
    co_return {};
  }
  TCFuture<void> fg_ScopeExit(CTarget &_Target)
  {
    auto Cleanup = g_OnScopeExit / [&] // warning: f_Destroy from g_OnScopeExit
      {
        _Target.f_Destroy();
      }
    ;
    co_return {};
  }
