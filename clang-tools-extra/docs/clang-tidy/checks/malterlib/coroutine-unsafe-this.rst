.. title:: clang-tidy - malterlib-coroutine-unsafe-this

malterlib-coroutine-unsafe-this
===============================

Finds ``TCFuture`` coroutines that use ``this`` of a class that does not
derive from ``CActor``, ``CActorInternal`` or ``CAllowUnsafeThis``: member
function coroutines that use their object, and coroutine lambdas that capture
``this``, or a pointer or reference into the object, ``[pValue = &m_Value]``,
also one a local view of ``ViewTypes`` holds in a borrowed member, ``[pItems
= Source.m_pItems]`` of ``CView Source{&m_Items}``, by what that member was
given (of a local record that isn't a view, a copy of it, and of any record
that isn't a view, an owner nor an actor, however it is reached, the value of
a view member stored in it is a value, never followed, and that of a pointer
or a reference member is where its class points it when the member states
that only its class points it, ``DMibSetByOwnClass``, and can't be told
otherwise, which may be the object, also as a getter of the object gives
it, ``CJob *pJob = f_GetJob();`` of ``return m_pJob;``, as reference capture
tells, :doc:`coroutine-reference-capture`; an owner made of what such a
member that doesn't state it points to, ``TCSharedPointer<CState>{m_pOther}``,
doesn't keep the object),
also of a copy
of it and through a cast, or that an accessor of it gives, ``[pItems =
&Source.f_Items()]``, not one into a copy's own storage, nor what a pointer
the object holds points to where it points elsewhere, ``[pData = m_pData]``,
nor an entry of a collection of it. A reference to the object or a member of
it captured by copy, ``[State]`` of ``auto &State = fg_Const(*this)``, copies
it into the closure, a value of its own: a use of a record that isn't a view
of ``ViewTypes`` that reads only its own part, ``Request.m_ID``, isn't one
of the object; one that reads past it, ``*Request.m_pData``, is, where a
pointer or a reference member of it, also of a record it holds by value, can
point into the object: where its class or the initializer of the member
record in the object's class points it there, ``CRequest
m_Request{&m_Value}``, or where what it points to can't be told, unless the
member states that only its class points it (``DMibSetByOwnClass``). A copy
given whole to a body that can't be seen, ``fg_Read(Request)``, and a
reference member read, ``State.m_Ref`` of ``CRef m_Ref{m_Value}``, are
reported too. A copy of a view refers where the view it copies does. Nothing keeps such an object alive while the coroutine is
suspended. An actor is kept alive, and its coroutines are cancelled when it is
destroyed.

.. example-before
   using umint = unsigned long;
   TCFuture<void> fg_Timeout(double _Seconds);

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
instantiated isn't checked. Of a generic lambda, the parameters held by value
of each specialization of its call operator, as instantiated, are among what
it holds.

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
rules. Any other use counts as a use after the suspension where what it makes keeps it
past it, told by the same rules as what a use does to the storage (below): kept
beyond the frame, in a member, a global, the result or a closure that runs
later, or held in a name of the frame, a local reference or pointer, a binding
or a capture of a closure that isn't called where it is made, that reads,
writes, gives or keeps it, wherever that name is. The address of a reference
follows the rules of a pointer: kept in a local that is only compared, ``CJob
*pLocal = &_Job; co_return pLocal == nullptr;``, or converted to an integer,
``reinterpret_cast<umint>(&_Job) != 0``, it touches nothing; an integer made of
it is its value, compared, tested, logged or returned as a number it touches
nothing, and converted back to a pointer it is the pointer again, ``auto
Address = reinterpret_cast<umint>(&_Value); co_await ...; co_return
*reinterpret_cast<int *>(Address);`` reads it. What is put in a
record held by value in the frame that isn't a view, an owner nor an actor is
carried by the member it is put in, by an aggregate, an assignment to the
member or a copy or move assignment the compiler defines: a later load of
that member, also through a getter whose body is seen, a copy the compiler
defines or a structured binding, is it again, and a load of another member,
the record's own storage, isn't; what a constructor or a member function
keeps of it (Rule R) is in the record as a whole. Such a record uses it where it is used or destroyed: a
call on it, a destructor that isn't trivial (seen, its body; not seen, it
uses what the record holds). One trivially destroyed and never used again
uses nothing of it, and so is an object returned by value. An awaiter
keeps what it holds while the coroutine is suspended; one that can't hold a
pointer, a reference or a view keeps nothing. Handles, like ``TCActor`` and ``TCPromise``, are copied by what
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
first, or when it also holds by value a keeper of the object: an owner of
``OwnerTypes`` (:doc:`/clang-tidy/MalterlibOptionsFile`) that owns the
class, a base of it or a class derived from it, captured by copy or in an
init capture, ``[this, pThis]`` of a ``TCSharedPointer<CState>`` or
``[this, pThis = f_Keep()]``, or a parameter of its call operator held by
value, also as a member, base or element of what it holds, ``[this, Keep]``
of a record holding one, decided by type, as reference capture does
(:doc:`coroutine-reference-capture`): a copy keeps through a ``shared``
owner, a move or a new value through a ``unique`` one too; a ``weak`` one
never. It keeps the object only where each value of it that reaches the
take, by the provenance's flow, is made of the object: an empty one
(``pThis.f_Clear()`` before the capture), one made of a pointer that can be
null on a way (``_bKeep ? this : nullptr``, also through a local pointer, or
a ``dynamic_cast`` to a derived class), one made of a pointer to another
object, or what a member function gives back called on another object,
``_pOther->f_Keep()``, doesn't keep; one made of ``fg_Explicit(this)`` or by
``f_Keep()`` called on the object does; one given by an owner parameter of
the member function, ``TCFuture<int> f_Run(TCSharedPointer<CState> _pPeer)``,
keeps it only where the parameter states that it owns the object,
``DMibKeepsThis TCSharedPointer<CState> _pSelf``, as it may own another; one
made of what a pointer member of the object points to keeps only where that
can be told (``DMibSetByOwnClass``); one given by a member function of the
object whose body can't be seen keeps only where the function states that
what it returns owns the object it runs on, ``DMibKeepsThis
TCSharedPointer<CState> f_Shared();``; a member of another object,
``_pHolder->m_pState``, and what a call that isn't on an object gives,
``fg_Other()``, also a new owner, ``fg_Make<TCSharedPointer<CState>>()``,
and a part of it, keep nothing of the object, as what they own can't be told
to be it. A capture the
closure's body can change, ``pThis = {};``, ``fg_Release(pKeep);`` of a
function that casts the const away, or a closure the body runs that clears
it, keeps only at the uses that each value reaching them keeps at; a
capture of a structured binding the body can change, ``pThis.f_Clear();``
of ``auto [pThis, ID] = ...``, keeps nothing. An array captured by copy,
``TCSharedPointer<CState> pStates[2]{_pPeer, _pPeer}``, keeps the object where
one of the elements it is made of does, and not where none can. A copy a
closure keeps of a member record bound to a local reference shares what its
pointer and reference members refer to: given whole where more than its own
part can be read, ``fg_Read(Request)`` of a body that can't be seen, or read
through a reference member, ``Ref.m_Ref``, it uses the object where those
can refer to it. And the closure is only given to functions that keep it alive while
it runs, like ``g_Dispatch /`` or ``fg_CallSafe``: a direct call of a
temporary, or a closure stored or returned, doesn't show that it is kept.
Each use of the object is also alive where the parameters of the call
operator held by value or the locals of the lambda's body in scope at the
use, declared before it in a scope still open there (also by an earlier declarator of the declaration whose initializer the use is in, ``TCSharedPointer<CJob> pKeep(this), pSelected = ...;``), hold such a keeper, ``auto pKeep = fg_FindSelf();``, also a
local reference that extends the temporary it is bound to, ``auto const &pKeep =
TCSharedPointer<CJob>{fg_Explicit(this)};``, which holds it as long as it is in scope, not
one bound to an owner that lives elsewhere, ``auto const &pKeep = _pOther;``, which is that
owner, and
the captures too when the closure is kept alive. For the other exemptions, the lambdas the lambda is made in must keep
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
``return`` returns, or what a ``co_return`` initializes the value type of the
future with, ``CGuard`` of ``TCFuture<CGuard>``, judged by type, since the
promise is plumbing and never looked into: a lambda given as an rvalue to the
conversion into another type is taken by it, ``co_return [this] { ... };``;
else what is made, as the type the function returns or the value type of the
future, by a constructor, also one that isn't written, ``return [this] { ...
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

The object of a member coroutine is alive where it is used when the context there holds by value a
keeper of it, by type as reference capture tells
(:doc:`coroutine-reference-capture`): its parameters held by value, the explicit object too, and the locals of its body held by value in scope at the use, declared before it in a scope still open there, each by its declared type, ``TCFuture<int> CJob::f_Run(TCSharedPointer<CJob> _pKeep)``; through a lambda that isn't a coroutine, called where it is made or later, ``auto fRead = [this] { return m_Result; };``, also what the code around it holds, out to the nearest coroutine. A keeper fits an anchor of the borrow, every object type on its derivation: of a use after a suspension the use itself, of a use before it that keeps a reference the whole expression it is in. Such a use is neither one after a
suspension nor one that keeps it, unless the result keeps it; the result is
judged by the type the promise stores, the ``T`` of the ``TCFuture<T>``: one
that can't hold a pointer, a reference or a view keeps nothing. Else it is
checked by itself, and keeps alive what it holds a keeper of by value, of
what is borrowed whatever it is, an owner's, the object or what a parameter
refers to, ``co_return CReadResult{_pKeep, &_Job.m_Result};``, also what a
member owner of the object owns, ``co_return CReadResult{m_pJob,
&m_pJob->m_Value};``; the keeper's own initializer is not on the derivation of
what it keeps, so an owner of an unrelated type put in is still reported. What a
function or a constructor keeps of what it is given is told by its body, or
of one whose body can't be seen by type, also through a pointer to a
function or a member function or any other callee, by its function type:
only its result, the object it makes or is called on by a member function
that isn't const, and what its other arguments given by reference or pointer
to what isn't const refer to can keep it, each when its type can hold a
pointer, a reference or a view; a const call operator of a function object
keeps nothing in it; an owner whose copy keeps what it owns, a weak owner and
a handle given by reference are copied. So ``CConnection
Connection(_pJob);`` of a shared owner, ``fg_Inspect(_Value);`` and
``_fInspect(_Value);`` through a ``void (*)(CJob const &)`` returning
``void``, ``Sink.f_Read(_Value)`` of a const member function and ``CValues
Values(_Value);`` of a class of values keep nothing.
A keeper keeps what an owner owns only when it owns the same object where it
is taken, as reference capture tells (:doc:`coroutine-reference-capture`).
Three misses are known: an owner released or reassigned between the borrow
and the use where it isn't in a local slot, an awaiter whose type can't hold a pointer, a reference or a view (list its type in ``ViewTypes`` to have it checked), and what a function or
constructor whose body can't be seen keeps elsewhere, a static or a
registry, aren't reported. A keeper given as a parameter, ``[this, pKeep =
_pOther]``, a keeper whose object can't be told, ``[this, pKeep =
_pHolder->m_pState]`` or ``[this, pKeep = fg_Other()]``, a member of a record read through a pointer to the record, a
record parameter held by value given the address of a member, and a copy of
a record that a closure captures by value are reported.

Nothing is reported in a translation unit with an error that stops it from
compiling: its control flow isn't built, and what the errors leave can't be
relied on.

.. example-check

  TCFuture<void> fg_Suspend();
  struct CInfo
  {
    int *m_pValue;
    int m_Count;
  };
  struct CIdAwaiter
  {
    CIdAwaiter(int const *_pValue);
    bool await_ready();
    void await_suspend(std::coroutine_handle<> _Handle);
    int await_resume();
    unsigned long m_Id;
  };
  void fg_Register(int const *_pValue);
  struct CState;
  struct CHolder
  {
    TCSharedPointer<CState> m_pState;
  };
  TCSharedPointer<CState> fg_Other();
  struct CState
  {
    int m_Value = 0;
    DMibKeepsThis TCSharedPointer<CState> f_Shared();
    TCSharedPointer<CState> f_Unstated();
    TCFuture<int> f_After() // warning: uses its object after a suspension
    {
      co_await fg_Suspend();
      co_return m_Value;
    }
    TCFuture<int> f_Before() // fine: uses its object only before its first suspension
    {
      int Value = m_Value;
      co_await fg_Suspend();
      co_return Value;
    }
    TCFuture<int> f_ThroughPointer() // warning: a member read through a pointer to the record
    {
      CInfo Info{&m_Value, 1};
      CInfo *pInfo = &Info;
      int *pValue = pInfo->m_pValue;
      co_await fg_Suspend();
      co_return *pValue;
    }
    TCFuture<int> f_Parameter(CInfo _Info) // warning: a record parameter given the address of a member
    {
      _Info.m_pValue = &m_Value;
      co_await fg_Suspend();
      co_return *_Info.m_pValue;
    }
    TCFuture<int> f_Captured() // warning: a copy of a record a closure captures by value
    {
      CInfo Info{&m_Value, 1};
      auto fRead = [Info]() -> TCFuture<int> { co_await fg_Suspend(); co_return *Info.m_pValue; };
      co_return co_await fRead();
    }
    TCFuture<int> f_Kept() // fine: a keeper of the object
    {
      return fg_CallSafe([this, pKeep = TCSharedPointer<CState>{fg_Explicit(this)}]() -> TCFuture<int> { co_await fg_Suspend(); co_return m_Value; });
    }
    TCFuture<int> f_ParameterKeeper(TCSharedPointer<CState> const &_pOther) // warning: a keeper given as a parameter
    {
      return fg_CallSafe([this, pKeep = _pOther]() -> TCFuture<int> { co_await fg_Suspend(); co_return m_Value; });
    }
    TCFuture<int> f_HolderKeeper(CHolder *_pHolder) // warning: a keeper that is a member of another object
    {
      return fg_CallSafe([this, pKeep = _pHolder->m_pState]() -> TCFuture<int> { co_await fg_Suspend(); co_return m_Value; });
    }
    TCFuture<int> f_CallKeeper() // warning: a keeper a call that isn't on the object gives
    {
      return fg_CallSafe([this, pKeep = fg_Other()]() -> TCFuture<int> { co_await fg_Suspend(); co_return m_Value; });
    }
    TCFuture<int> f_UnstatedKeeper() // warning: a member function that doesn't state it gives an owner of its object
    {
      return fg_CallSafe([this, pKeep = f_Unstated()]() -> TCFuture<int> { co_await fg_Suspend(); co_return m_Value; });
    }
    TCFuture<int> f_StatedKeeper() // fine: a member function that states it gives an owner of its object
    {
      return fg_CallSafe([this, pKeep = f_Shared()]() -> TCFuture<int> { co_await fg_Suspend(); co_return m_Value; });
    }
    TCFuture<int> f_Awaiter() // clean: a known miss, an awaiter whose type can't hold a pointer
    {
      co_return co_await CIdAwaiter(&m_Value);
    }
    TCFuture<int> f_Unseen() // clean: a known miss, kept elsewhere by a body that can't be seen
    {
      fg_Register(&m_Value);
      co_await fg_Suspend();
      co_return 0;
    }
  };
  struct CRequest
  {
    int *m_pData;
  };
  struct CRef
  {
    int &m_Value;
  };
  void fg_Read(CRequest _Request);
  struct CRequests
  {
    int m_Value = 0;
    CRequest m_Request{&m_Value};
    CRef m_Ref{m_Value};
    TCFuture<int> f_Read() // warning: a copy whose member the class points into the object
    {
      auto &Request = m_Request;
      return fg_CallSafe([Request]() -> TCFuture<int> { co_await fg_Suspend(); co_return *Request.m_pData; });
    }
    TCFuture<int> f_OwnPart() // fine: a copy that reads only its own part
    {
      auto &Request = m_Request;
      return fg_CallSafe([Request]() -> TCFuture<int> { co_await fg_Suspend(); co_return Request.m_pData != nullptr; });
    }
    TCFuture<int> f_Given() // warning: a copy given whole to a body that can't be seen
    {
      auto &Request = m_Request;
      return fg_CallSafe([Request]() -> TCFuture<int> { co_await fg_Suspend(); fg_Read(Request); co_return 0; });
    }
    TCFuture<int> f_Reference() // warning: a reference member read
    {
      auto &State = *this;
      return fg_CallSafe([State]() -> TCFuture<int> { co_await fg_Suspend(); co_return State.m_Ref.m_Value; });
    }
  };

Options
-------

.. option:: FutureTypes

   Semicolon-separated list of the futures, whose destruction doesn't end
   what makes their results, matched like the other type lists, also through
   aliases, ``TCUnsafeFuture``. None are built in; the Malterlib options file
   (:doc:`/clang-tidy/MalterlibOptionsFile`) adds Malterlib's, like
   `TCFuture`.
