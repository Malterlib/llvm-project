.. title:: clang-tidy - malterlib-coroutine-unsafe-this

malterlib-coroutine-unsafe-this
===============================

Finds ``TCFuture`` coroutines that use ``this`` of a class that does not
derive from ``CActor``, ``CActorInternal`` or ``CAllowUnsafeThis``: member
function coroutines that use their object, and coroutine lambdas that capture
``this``. Nothing keeps such an object alive while the coroutine is
suspended. An actor is kept alive, and its coroutines are cancelled when it is
destroyed.

.. code-block:: c++

  struct CHelper
  {
    TCFuture<void> f_Run()  // warning
    {
      co_await fg_Timeout(1.0);
      ++m_nRuns;
      co_return {};
    }

    umint m_nRuns = 0;
  };

A template is checked as each of its instantiations makes it, like a member
function of a class template whose base depends on a template parameter,
which may be an actor, and a generic lambda whose result depends on what it is
called with, ``[this](auto _Future) -> decltype(_Future)``; one that isn't
instantiated isn't checked.

A member function is called directly, so it runs while the caller has the
object until it first suspends: only uses of the object after a suspension are
reported, also in a lambda it makes, but not in one called where it is made,
``[this] { return m_Value; }()``, that isn't a coroutine itself. An awaiter whose ``await_ready`` only returns true, like that of ``co_await
g_AccessCoroutine``, doesn't suspend, unless it is virtual, which an
override can make suspend, and neither does one whose ``await_suspend`` only
returns false, which resumes right away. Naming a static member through the object,
``this->ms_Value`` or ``this->fs_Get()``, also chosen by a conditional,
``(bChoose ? this : pOther)->ms_Value``, doesn't use it. Before the first suspension, the object may only be used as values:
read as scalars, copied or moved into values that are not views, or given to
functions that return a ``TCFuture``, whose coroutines are held to the same
rules. Any other use, like binding a reference to it, taking its address,
making a view, an awaiter or another object from it, or giving it by reference
to another function, counts as a use after the suspension, since what it makes
can keep a reference past it. Objects returned by value are values, unless they
are awaited. Handles, like ``TCActor`` and ``TCPromise``, are copied by what
they are given to, and so is what is given to them, like the arguments of an
actor call. Moving the object into the coroutine with ``co_await
fg_MoveThis(*this)`` before that, into a variable of its class, ``auto This =
co_await fg_MoveThis(*this)`` (not a reference or a view made of it), is fine too, and one that never runs, in a
discarded branch or one a constant condition skips, ``if (false)``, doesn't
use the object.

A lambda that captures ``this`` is fine when it is called directly, or through
``fg_CallSafe``, and uses the object only before its first suspension, when it
is given only to a test category, which dispatches it and waits for it, but
not when it is joined with ``f_CallSync()``, where what is between can leave
first, or when it also captures a strong owning pointer to
the object, like ``[this, pThis =
TCSharedPointer<CState>{fg_Explicit(this)}]``, or a copy of an unchanged local
one, like ``[this, pThis]``, which keeps the object alive
when the lambda doesn't reset, assign or move it, also through a pointer to
it, ``*const_cast<...>(&pThis) = ...``, or a reference bound through a
conditional (through more references to it than can be looked through, it is
taken to be reset), and the closure is only given
to functions that keep it alive while it runs, like ``g_Dispatch /`` or
``fg_CallSafe``: a direct call of a temporary, or a closure stored or returned,
doesn't show that it is kept. A weak pointer, or one to another object,
doesn't. For the other exemptions, the lambdas the lambda is made in must keep
the object too: be called directly and not be coroutines, or hold a copy of it,
``[*this]``.

In builds with safe checks, the runtime reports such coroutines when they are
called directly instead of through an actor call, a dispatch or
``fg_CallSafe``. The check moves this to compile time. Derive the class from
``CAllowUnsafeThis`` to state that its object outlives the coroutines, or
return a ``TCUnsafeFuture`` to opt out of the protections.

The object of a member function that runs to completion, declared with
``DMibRunsToCompletion`` (see :doc:`malterlib-runs-to-completion
<runs-to-completion>`), is kept by its callers until they join it, like a
reference parameter, also for the lambdas it starts and joins, and is not
reported, except what such a lambda's result keeps of it, also through a
captured pointer to it, like ``co_return &pThis->m_Value``.

A lambda whose introducer comes from a macro, like the ``[&] ()`` of
``DMibTestCategory("Name") % [&] () { ... }``, is reported where the macro is
used, in the code that writes the rest of the lambda.
