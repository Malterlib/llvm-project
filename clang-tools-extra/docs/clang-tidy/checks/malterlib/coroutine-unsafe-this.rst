.. title:: clang-tidy - malterlib-coroutine-unsafe-this

malterlib-coroutine-unsafe-this
===============================

Finds ``TCFuture`` coroutines that use ``this`` of a class that does not
derive from ``CActor``, ``CActorInternal`` or ``CAllowUnsafeThis``: member
function coroutines that use their object, and coroutine lambdas that capture
``this``, or a pointer or reference into the object, ``[pValue = &m_Value]``,
also one a local holds in a borrowed member, ``[pItems = Source.m_pItems]``
of ``CView Source{&m_Items}``, by what that member was given, also of a copy
of it and through a cast, or that an accessor of it gives, ``[pItems =
&Source.f_Items()]``, not one into a copy's own storage, nor what a pointer
the object holds points to, ``[pData = m_pData]``, nor an entry of a
collection of it. A reference to the object captured by copy, ``[State]`` of
``auto &State = fg_Const(*this)``, copies it into the closure. Nothing keeps such an object alive while the coroutine is
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
TCSharedPointer<CState>{fg_Explicit(this)}]``, made of what can't be null on
any path, ``this``, its address or an unchanged local alias of one, also a
conditional of such, ``bFirst ? this : this``, not what
can, ``_bKeep ? this : nullptr``, also read back through its address,
``*&pMaybe`` or ``&*pMaybe``, or a ``dynamic_cast`` to a pointer that
checks, which gives null when the object isn't of the class, not one to the
class itself or a base, also through a helper, or a copy of an
unchanged local one, like ``[this, pThis]``, which keeps the object alive
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

A member function whose result must not outlive its object, annotated
``[[clang::annotate("malterlib-result-ends-before-object")]]``, which
Malterlib's ``DMibResultEndsBeforeObject`` writes, can give a guard whose lambda
uses ``this`` when the guard is destroyed, like ``CGitLaunches::f_Init``
returning ``fg_AsyncDestroyGeneric([this]() -> TCFuture<void> { ... })``. Such
a lambda, of a function annotated so, as is each function it overrides,
through whose callers the result can otherwise come, one that is part of what
the function returns, told from what it returns: past parentheses, cleanups,
temporaries and implicit conversions that aren't user-defined, what a
``return`` returns, or what a ``co_return`` gives the promise's
``return_value``, also after the promise of one with an explicit object
parameter, ``return_value(this CPromise &, CGuard)``, is made, as the type the
function returns or the promise takes, or of one whose parameter is
deduced, also a forwarding reference, the value type of the future, ``CGuard`` of ``TCFuture<CGuard>``, by a constructor, also one that isn't written, ``return [this] { ...
};`` of a ``CGuard``, also in braces, ``co_return {[this] { ... }, 1};``,
also of an aggregate, also with a designator, ``CGuard{.m_fDestroy = [this]
{ ... }}``, or by a call of a function that isn't a member, also through a
pointer to one, ``auto fFactory = &fg_MakeGuard; return fFactory([this] { ...
});``, and the lambda is
one of its direct arguments, also converted to the parameter by a
constructor, ``fg_AsyncDestroyGeneric(FUnitVoidFutureFunction &&)``. Not
one in another call, ``fg_Count([this] { ... })``, nor what is converted to
what isn't an object, ``static_cast<int>(...)``, or by a user-defined
conversion operator, nor one whose result isn't an object of a class,
like a token of a registration, ``int f_Register()`` returning
``fg_Register([this] { ... })``, which ends nothing when it is destroyed, nor
an argument or the object of a member function or a member operator,
``fg_Wrap([this] { ... }).f_Finish()``, nor what is called,
``[this](int) { ... }(co_await ...)``, nor a value of a conditional, the
left of a comma, or what is in a statement expression. Such a lambda is
accepted when it
uses the object only before it first suspends, also in the check of the
function itself, which doesn't count what the lambda's body does, though it
counts what its captures are made of, ``[Value = m_Value]``, which runs in the
function; each function it overrides is gone through once, and none is
exempt when they can't all be; one it gives
elsewhere, like to
a queue, is checked like that of any other function. Each call of the
function, also in the instantiations of a template, ``_Launches.f_Init()``,
also through a member pointer that isn't changed, ``(Launches.*fInit)()``,
only a local of the function the call is in, not a static one, a global
or a member, and only in a call outside the body of a lambda,
not a parameter, whose default isn't what it is given,
also made through an explicit cast of the address, also in braces,
``auto const fInit{&CLaunches::f_Init};``, or a copy of a constant one, ``constexpr auto fCopy = fInit;``, or the cast address itself,
or a function pointer to one with an explicit object parameter,
``fInit(Launches)`` or ``(*fInit)(Launches)``,
only read, also in parentheses, or not evaluated, but in the body of a
lambda, which runs when it is called, also one in what isn't evaluated,
``decltype([] { ... })``, ``sizeof(fInit)`` or
``noexcept(fInit)``, and not named anywhere where a lambda in the function is
written, from its ``[`` to its closing ``}``, also in its template
parameters, ``[]<auto t_fTarget = fInit>()``, or its signature, ``[]() ->
decltype(fInit)``, by where each is spelled, also in a macro, or anywhere in
a lambda, also one a macro writes, not by where a macro that writes both is
expanded, so a use beside a lambda a macro writes, ``#define CALL(p, o) ([]
{}, (o.*p)())``, is still told, also one in
what isn't evaluated, ``decltype([&] { fInit = ...; })``, captured or used,
whatever it does with it, ``[fInit]() mutable { fInit = nullptr; }``, also
when it is const or constexpr,
also of a member operator, ``Launches()``, or of a member with an explicit
object parameter, but not one that isn't evaluated, ``decltype(
Launches.f_Init())``, ``__builtin_constant_p(Launches.f_Init())`` or
``__builtin_assume(...)``, also in the body of a lambda, nor one that never
runs, after what always leaves, ``return; Object.f_Init();``, also by
throwing or by calling a ``[[noreturn]]`` function, in a branch a
constant condition never takes, ``if (false)``, also one
whose result has cleanups, or in parentheses, ``false && (...)``, also what
the captures of a lambda there are made of, ``if (false) (void)[Guard =
Object.f_Init()] {};``, and a
member pointer assigned only there, or in what a constant left side
short-circuits, ``false && (fInit = ...)``, is still unchanged, is checked to keep its result in a local declared after the object, itself a
local and not one a pointer points to, in the same block, so the result is
destroyed first: ``CGitLaunches Launches{...}; auto DestroyLaunches = co_await
co_await Launches.f_Init();``. A future, a result of a type :option:`FutureTypes`
lists, ``TCUnsafeFuture<CGuard>``, whose destruction doesn't end what makes
its result, ends first only when the call is directly the operand of
``co_await``, also in parentheses, ``co_await Launches.f_Init()``, and so on
while what an await gives is a future too, ``co_await co_await
Launches.f_Init()`` of one that gives a future of a future, also after an
await of what isn't one, an awaiter whose await gives a future; what the
last direct await gives is what the local keeps, which is never a future,
also one an await of a local gives, and a future on the way to it, also
after an await, is awaited directly, not moved, ``co_await
fg_Move(co_await fg_Move(Launches.f_InitStage()))``. A future still left when the awaits go
deeper than they are followed, 16, is reported. A type that is only
awaitable isn't a future. Any other use of a future, like
keeping it, ``auto Pending = Launches.f_Init();``, also to await it later, is
reported, with no following of where it goes. The local owns the result: it is of the type
of the result, or of what the last await of it gives, which gives it by value,
also moved, ``fg_Move(...)``, initialized in braces by it, ``CGuard
Guard{Launches.f_Init()}``, made of it as its type without a construction,
``CGuard(Launches.f_Init())``, or what a move constructor of its type, also one
with trailing default arguments, makes of it,
``CGuard(fg_Move(Launches.f_Init()))``, not of a base, which slices it,
not a pointer, also not a reference to one, ``auto &&pGuard =
Launches.f_InitPointer();``, which extends the life of the pointer, not of
what it points to, nor a reference other than to the temporary whose life it
extends, ``auto &&Guard = Launches.f_Init();``, nor what another constructor
makes of it, also of what an await gives, ``CHolder Holder = co_await co_await
Launches.f_Init();``, nor a structured binding, ``auto [pValue] =
Launches.f_Init();``, whose bindings aren't followed. It must not leave the
block, and only two uses of it are known not to let it: a call of one of its
member functions, also one with an explicit object parameter that refers to
it, ``DestroyLaunches.f_Clear()``, also in parentheses,
``(DestroyLaunches).f_Clear()``, and an await of it moved, ``co_await
fg_Move(DestroyLaunches);``, whose result, followed through its direct
awaits like the result of the call, so that a future it gives is awaited
directly, is discarded, also cast to void, ``(void)co_await
fg_Move(DestroyLaunches);``, also as the body of
an ``if`` or a loop without braces, or the init statement of an ``if``, a
``for`` or a ``switch``, ``if (co_await fg_Move(DestroyLaunches); bRun)``, or kept by such a local
in the block, an automatic one, which is checked the same way, as awaiting a future of a guard
gives the guard, ``co_return co_await fg_Move(DestroyAwaiter);`` escapes, also through a move constructor of its type, not
another constructor, ``co_await CEscapingAwaiter(fg_Move(DestroyLaunches))``. Any other use, like returning it, moving it
elsewhere, capturing it by a lambda, giving it to a function, or using a data
member of it, ``Guard.m_fCleanup``, also to a cleanup function,
``__attribute__((cleanup(fg_Save))) auto Guard = Launches.f_Init();``, which
is given its address when it goes, is reported.

Options
-------

.. option:: FutureTypes

   Semicolon-separated list of the futures, whose destruction doesn't end
   what makes their results, matched like the other type lists, also through
   aliases, ``TCUnsafeFuture``. None are built in; the Malterlib options file
   (:doc:`/clang-tidy/MalterlibOptionsFile`) adds Malterlib's, like
   `TCFuture`.
