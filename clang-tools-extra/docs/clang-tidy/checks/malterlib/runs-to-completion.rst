.. title:: clang-tidy - malterlib-runs-to-completion

malterlib-runs-to-completion
============================

Verifies the contract of ``DMibRunsToCompletion`` from
``Mib/Concurrency/TidyAnnotations``. Code that runs to completion only ends by
finishing, never by being aborted: it only suspends on actors that are not
destroyed while it runs, like the concurrent and blocking actors, and whoever
starts it joins it before what it refers to is gone. That lets it refer to the
data of its callers, like a ``TCUnsafeFuture``, while the other checks keep
checking the rest of it.

.. code-block:: c++

  // A function, or all member functions of a class, which isn't an actor.
  DMibRunsToCompletion TCFuture<void> fg_UpdateRemotes(CBuildSystem &_BuildSystem);
  struct DMibRunsToCompletion CBuildSystem { ... };

  // Where it begins: switches to a concurrent actor before it first suspends.
  DMibRunsToCompletionEntry TCFuture<ERetry> fs_RunBuildSystem(TCFunction<...> _fCommand);

The check verifies:

- A ``DMibRunsToCompletion`` function that returns a future is only called
  (not in a discarded branch of an ``if constexpr``, nor in a branch a
  constant condition never takes that nothing jumps into, which doesn't run)
  where code runs to completion: in another one, in an entry, in a lambda
  given to one of them as an argument, which they run in their context (an
  entry only after it has switched: each use of its parameter must be after
  the switch, or give it on to another entry; an entry defined in another
  file is trusted), or in
  a lambda that such code starts and joins without dispatching it to an actor
  that may be destroyed, of which every use is such a call, also of copies
  made by init captures: ``g_Dispatch / ...`` dispatches to the current
  actor, which is concurrent only after an entry has switched, and
  ``fg_ConcurrentDispatch`` to a concurrent one. Before an entry switches, it
  runs on the actor of its caller, so it may not call such functions yet.
  After awaiting another entry first, it resumes on the actor of its caller
  too, until it switches itself, in a statement of its body, and until then
  what it starts and joins can't rely on its locals, which an abort can
  destroy, like a reference capture of a lambda given to ``fg_CallSafe``.
- Each call is joined, so it ends before what it refers to: ``co_await`` in
  its full-expression, also through ``.f_Wrap()``, ``.f_Move()``, ``% "Context"`` and a pack
  of futures, ``fg_A() + fg_B()``, whose other parts don't suspend (an await
  whose awaiter never suspends, ``co_await CSuspendNever{}``, or that runs
  only in a value of a conditional that a constant condition doesn't choose,
  doesn't), or its
  future is collected into a local, like ``> Results``, ``> Results[Key]``,
  ``Results.f_Insert(...)`` or ``auto Result = ...`` (assigning a slot,
  ``Results[Key] = ...``, isn't, since it can drop a future that was there),
  that an ``fg_AsyncDestroy`` guard declared after the local and before the
  call awaits in its cleanup: as a statement of the cleanup's body, also
  ``co_return co_await fg_AllDone(Results)``, also with context, like
  ``co_await (fg_AllDone(Results) % "Joining")``, before anything that can
  branch (an ``fg_AllDone`` of the collection elsewhere must be awaited, since
  it empties it), return or suspend, or throw in a cleanup that captures
  exceptions (a call, like ``fg_Log("Joining")``, can only throw), of the
  collection captured by reference,
  with a local guard (not ``static``), also kept in a reference that extends
  it, ``auto &&Join``, that is never cleared or moved, and a collection that
  isn't replaced, also in parentheses, ``((Results)) = ...``.

  A throw doesn't leave without a join: in a coroutine that doesn't capture
  exceptions, what isn't caught in it ends the program. A coroutine that
  captures exceptions, by its flags, ``TCFutureWithFlags<int,
  ECoroutineFlag_CaptureExceptions>``, also given when forwarding the object,
  ``fg_MoveThis<ECoroutineFlag_CaptureExceptions>(*this)``, or by setting them or a scope that
  captures them, ``co_await ECoroutineFlag_CaptureExceptions`` or ``co_await
  g_CaptureExceptions``, also in what a lambda captures, which runs in the
  coroutine, and a ``try`` in the coroutine around the call, can
  leave by a throw, which the check doesn't follow: there, a local can't rely
  on an early join, below. Neither can a throw before a blocking join,
  ``f_CallSync()``, which isn't a join (see
  :doc:`malterlib-coroutine-reference-capture <coroutine-reference-capture>`).

  The guard starts its cleanup where its scope ends, but the coroutine only
  joins the cleanup when it finishes, after its locals are gone: with a guard,
  what the reference and pointer arguments refer to must outlive the
  coroutine, like its parameters, the data of its callers, its object or what
  has static storage, or be a local whose scope the guard is awaited early in,
  with ``co_await fg_Move(Guard)`` before the scope ends, on every path: as a
  statement of the scope or of blocks in it, not in a branch or a ``try``,
  with nothing between the call and it that can leave the scope, like a
  ``return``, a jump out of it, or an await that can abort the coroutine: one
  of a future, or of a pack of them, ``co_await (fg_A() + fg_B())``, also of
  the awaiter of either made explicitly, ``.operator co_await()``, which
  aborts when it fails, also of one with context, ``co_await (fg_A() %
  "Context")``, an actor call or an async result, also one that is ready without suspending (but not
  one of ``.f_Wrap()``, which gives the failure as a value, also kept in an
  unchanged local, ``auto Wrapped = fg_Move(Future).f_Wrap(); co_await
  fg_Move(Wrapped);``, nor a switch to a concurrent actor), and any
  suspension while a callback of ``fg_OnResume``, which can abort the
  coroutine where it resumes, is registered by a registration held in the
  scope, or a temporary one earlier in the same full expression, which is
  held until its end, also by a phase guard, like ``co_await f_CheckDestroyedOnResume()``,
  also of another actor, and what a function whose body can be seen returns
  of one, ``auto fg_Watch() { return fg_OnResume(...); }``, also through
  any number of them,
  also through an awaiter made before, ``co_await fg_Move(Awaiter)``, also
  forwarded, ``fg_Forward<decltype(Awaiter)>(Awaiter)``, and what it was last
  assigned, ``Pending = fg_Later()``, also a
  reference to one, or either of a conditional, also of what is awaited,
  ``co_await (bChoose ? fg_OnResume(A) : fg_OnResume(B))``, and so can the registration itself, which runs the
  callback (not what is in a discarded branch of an ``if constexpr``, nor what
  isn't evaluated). A local of an init statement lives only as long as that
  statement, and so doesn't live until an early await after it. What runs
  after the call can leave too: a loop condition that runs again, and what is
  written before it but not sequenced before it, like the left side of an
  assignment, ``Slots[co_await fg_Index()] = (fg_Read(Local) > Results, 0)``,
  but not an earlier element of a braced list, which runs in order, nor the
  left side of an assignment whose right side has the early await, which runs
  after it. An
  explicit object parameter of an entry, ``this CEntry Self``, is one of its
  parameters. The early await can be through
  a local reference to the guard. This applies also to what a local pointer or
  reference refers to, the data of a view
  or pointer given by reference as well as the variable, both values of a
  conditional, also what an unchanged local pointer was made from, ``int
  *pValue = bChoose ? &First : &Second``, the elements of a local array, also through pointer
  arithmetic, what a cast of a pointer or reference is of,
  ``static_cast<int const *>(&Value)``, the object a structured binding
  decomposes, a parameter by value of a coroutine around the lambda that has
  the guard, a member of the object of a function that runs to completion, or
of what an unchanged pointer parameter of one points to, ``[&Value =
_pState->m_Value]``, a
  view made of a local array and a size (but what a view kept in a member, or a
  pointer or view that isn't local, like a global, refers to can't be told),
  and the object of a member
  function or operator too, and what a default argument is made of,
  ``fg_Read()`` of ``fg_Read(int const &_Value = g_Global)`` (a string literal, which lives as long as the
  program, and a null pointer are fine, also as a value of a conditional,
  ``bChoose ? &Local : nullptr``, and only the value a constant condition
  chooses is given): not a temporary, unless a local reference
  extends it, ``CValue const &Value = CValue{1}``, nor what a pointer that
  another pointer points to points to. A guard awaited early, like ``co_await
  fg_Move(Guard)``, joins only what was started before it, in an earlier
  full expression (one in the same, ``Slots[(fg_Read(Local) > Results, 0)] =
  (co_await fg_Move(Guard), 0)``, can run before the call), so it isn't
  trusted in a function with a ``goto``, nor in a loop around the call and it,
  unless the guard is made in the loop, a new one each time. A local of a function around a
  lambda that has the guard counts when each lambda in between is joined in
  its full-expression.
- A ``DMibRunsToCompletionEntry`` is a coroutine that first suspends at ``co_await
  fg_ContinueRunningOnActor(...)``, also on the left of a comma, ``co_return
  (co_await fg_ContinueRunningOnActor(...), 1)``, with a concurrent or blocking actor, like
  ``fg_ConcurrentActorLowPrio()``, ``fg_ThisConcurrentActor()``,
  ``fg_DynamicConcurrentActor()``, also through an unchanged local, also
  moved into the switch, ``fg_Move(Actor)``, or a
  ``CBlockingActorCheckout`` parameter taken by value and unchanged, or at a
  call of another entry (also
  through ``.f_Wrap()`` and ``% "Context"``), on every path: not in a branch
  (other than the kept one of ``if constexpr``, or of a constant condition,
  ``if (false)``, which never runs the other, or the one of ``if consteval``
  that runs outside of a constant evaluation, ``if !consteval``, where a
  coroutine always runs), a loop or a ``try``. Templates are checked in their instantiations.
- Code that runs to completion doesn't ``fg_ContinueRunningOnActor`` to an
  actor that may be destroyed (a switch kept in a local that nothing uses is
  never awaited, and doesn't switch), nor uses it without calling it, like through a
  pointer, where the actor can't be told, nor awaits a transfer made
  elsewhere, like one given as a parameter, also through a wrapper whose
  ``operator co_await`` gives its awaiter (one kept in an unchanged local,
  also through local references to it, ``auto &Alias = Transfer``, is told). Calling other actors is fine: if
  they are destroyed, the call fails instead of aborting the caller.

What the other checks then accept:

- :doc:`malterlib-coroutine-reference-parameter <coroutine-reference-parameter>`:
  the reference and pointer parameters of a function that runs to completion,
  unless it is a member function of an actor, which actor calls call. What its
  result keeps of them, like ``co_return &_Value``, also through a local,
  still outlives the join.
- :doc:`malterlib-coroutine-unsafe-this <coroutine-unsafe-this>`: the object
  of a member function that runs to completion, like an implicit reference
  parameter, also in the lambdas it starts and joins, except what their
  results keep of it.
- :doc:`malterlib-coroutine-reference-capture <coroutine-reference-capture>`:
  the reference captures of a lambda that code that runs to completion starts
  and joins, of what lives until the join (except what its result keeps of
  them, like ``co_return &Value``): with a guard, the locals whose scope
  the guard is awaited early in; and the parameters, of which references and
  pointers only of a function that runs to completion. The cleanup of an
  ``fg_AsyncDestroy`` in such code may capture the parameters of the
  coroutines around it, which live until they finish, but not of a factory
  that returned it, and only when its guard is a local that is only
  destroyed, awaited early or cleared: a guard that is returned runs the
  cleanup after they are gone. Its locals are gone when the coroutine joins
  the cleanup, unless the guard is awaited early in their scope.

.. code-block:: c++

  DMibRunsToCompletion TCFuture<void> fg_UpdateRemotes(CBuildSystem &_BuildSystem)
  {
    TCFutureVector<void> Results;
    auto JoinResults = co_await fg_AsyncDestroy
      (
        [&]() -> TCFuture<void>
        {
          co_await fg_AllDone(Results).f_Wrap();
          co_return {};
        }
      )
    ;
    g_Dispatch / [&_BuildSystem]() -> TCFuture<void> { ... } > Results; // OK
    CStr Name = _BuildSystem.f_Name();
    fg_UpdateName(Name) > Results; // OK only with the early await below
    co_await fg_Move(JoinResults); // Joins while Name is there
    co_return {};
  }
