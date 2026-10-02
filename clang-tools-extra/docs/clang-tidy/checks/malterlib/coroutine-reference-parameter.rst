.. title:: clang-tidy - malterlib-coroutine-reference-parameter

malterlib-coroutine-reference-parameter
=======================================

Finds parameters of ``TCFuture`` coroutines that refer to data the caller
owns, like references, pointers, spans and string views, and that are used
after a suspension, when the data can be gone (not what a view taken by value
has of its own, which is in the frame, like its length, ``_Text.m_Length``,
also a member of only values, ``_Span.m_Info.m_nBytes``, and what a member
of it by value that holds a view has of its own, ``_Source.m_Line`` or
``_Source.m_Text.f_GetLen()`` of ``struct CSourceStrSpan { CStrSpan m_Text;
umint m_Line; }``, also updated, ``_Text.m_Length = 0``, or given by reference,
``CStr::CFormat("{}") << _Text.m_Length``, or a const member function of it that
gives only that, ``_Text.f_GetLen()``, or the pointer it holds, compared
(``==``, ``<``, ``<=>``) or tested, also as such a member function gives it, also through the getter of a
base it forwards to, ``_Buffer.f_GetStr() != nullptr`` of ``CStrPtr``, whose
``f_GetStr()`` returns ``CImp::f_GetStr()``, or a reference a member function
whose body can be seen gives into its own storage, ``_Source.f_GetPosition().m_Line``
of ``return m_Position;``; a pointer into the data that is
kept past a suspension, also as what a lambda given a copy of the view
returns, is a use of the data). What a lambda called where it is made
returns, ``co_return [&_Value] { return &_Value; }();``, is the result.
This is a best effort check.

.. example-before
   struct CStr { CStr(); CStr(CStr const &_Other); ~CStr(); };
   struct CStrSpan { char const *m_pStr; unsigned m_Length; };
   TCFuture<void> fg_Timeout(double _Seconds);
   TCFuture<void> fg_Write(CStrSpan _Data);
   TCFuture<void> fg_Write(CStr _Data);

.. code-block:: c++

  TCFuture<void> fg_Send(CStrSpan _Data)  // warning
  {
    co_await fg_Timeout(1.0);
    co_return co_await fg_Write(_Data);
  }

  TCFuture<void> fg_Send(CStr const &_Data)  // fine
  {
    co_return co_await fg_Write(CStr(_Data));
  }

A coroutine runs in its call until it first suspends, while the caller has the
data, so it may use such parameters until then. An awaiter whose ``await_ready`` only returns true, like that of ``co_await
g_AccessCoroutine``, doesn't suspend, unless it is virtual, which an
override can make suspend, also a static one, and neither does one whose
``await_suspend`` only returns false, which resumes right away. Naming a static
member through one, ``_pState->ms_Value`` or ``_pState->fs_Get()``, doesn't use
the data. Before the first suspension,
the data may only be used as values: read as scalars, copied or moved into
values that are not views, or given to functions that return a ``TCFuture``,
whose coroutines are held to the same rules. Any other use counts as a use after the suspension where what it makes keeps it
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
keeps of it (Rule R) is in the record as a whole. A temporary record a
member function whose body is seen keeps it in, and gives back as itself,
``return *this;``, is that result, which holds it as a whole, so a named
record moved from it carries it, ``CFormat Format = CFormat("{}") <<
_Text;``. Such a record uses it where it is used or destroyed: a
call on it, a destructor that isn't trivial (seen, its body; not seen, it
uses what the record holds). One trivially destroyed and never used again
uses nothing of it, and so is an object returned by value. An awaiter
keeps what it holds while the coroutine is suspended; one that can't hold a
pointer, a reference or a view keeps nothing. Handles, like
``TCActor`` and ``TCPromise``, are copied by what they are given to, and so is
what is given to them, like the arguments of an actor call. What the first
``co_await`` awaits runs before it suspends; a use in a lambda the coroutine
makes counts as after a suspension (but not one called where it is made, ``[&Value] { return Value; }()``, also
through its call operator by name, ``.operator()()``, or ``std::invoke``,
that isn't a coroutine itself, which runs there), and so does the object of a member
function whose arguments suspend, ``_Value.f_Read(co_await fg_Work())``, also
through what a pointer to it points to, ``(*_pValue).f_Read(...)``
(not by an await that a constant condition skips, ``false ? co_await
fg_Work() : 1`` or ``true || (co_await fg_Work(), true)``),
also through a pointer to a member function, ``(this->*fRead)(...)``, which
the function uses after them, and what an argument given by reference or
pointer refers to, ``fg_Read(_Value, co_await fg_Work())``, and what a pointer
points to accessed after an await in the same expression, ``_pValue[(co_await
fg_Work(), 0)]``, also through ``->`` or a cast of the pointer,
``(_pValue + co_await fg_Offset())->m_Value``, also through a
cast to a reference or ``fg_Move(_Value)``, also through a
pointer to a function, also when what
suspends is what initializes the captures of a lambda given to it. What ``co_return`` gives outlives the
call, so it must be a value too, also in a coroutine that doesn't suspend,
where only what the result is made from matters, also through a local, ``int
*pValue = &_Value; co_return pValue;`` (not when it is assigned something else
before each ``co_return`` of it, ``pValue = nullptr``, also in each branch of
an ``if`` that ends assigning it, but still when what can change it otherwise
comes after, ``fg_Set(pValue, _Value)``), also through any number of copies of
it or as either of a conditional, ``(bFirst ? pFirst : pSecond) = &_Value``,
not a pointer only read through,
``int Copy = *pValue; co_return nullptr;`` (what is read as a scalar on the
way, ``_Value ? g_pData : nullptr``, is a value), and
so must the result it is stored as, also through other locals and the elements
of arrays, what a reference member is bound to, also through a conditional,
``CResult{bChoose ? _Value : g_Value}``, a comma, an assignment, an increment
or a cast, what a function whose body can be seen gives back of it,
``CResult{fg_Const(_Value)}``, and the value a constant condition selects, ``false ?
CResult{_Value} : CResult{g_Value}``, and a copy of a view kept in it, like ``CResult{_Text}``, also of a
local copy of it, ``auto Copy = _Text;`` (but not
what a constructor that can be looked into, also through the constructor of a
base or one it delegates to that does, only copies into members that hold
values, ``m_Value(_Value)``, also of a trivially copyable class of values,
``CState m_State``, or reads as a number in its body, ``m_Value =
_Value;``, also through a local pointer or reference of it, ``m_Value =
*pValue`` of ``int const *pValue = &_Value``, or keeps it only in a branch a
constant condition never takes, ``if (t_bKeep) m_pValue = &_Value;``, nor
what an owner parameter owns when the result holds by value a keeper of
it, by type as reference capture tells
(:doc:`coroutine-reference-capture`), ``co_return CReadResult{_pJob,
&_pJob->m_Value};`` (a keeper that owns another object of a fitting type
isn't reported), nor a
``co_return`` in a discarded branch of an
``if constexpr``). A pointer parameter that a statement of the body points at other
data, like ``_pValue = &Local``, also with a condition that only chooses what,
``_pValue = _pValue ? &g_First : &g_Second``, also each branch of an ``if`` that ends
assigning it, ``if (_bChoose) _pValue = &g_First; else _pValue = &g_Second;``,
and nothing else assigns, also through a cast
to a reference, ``fg_Reset(static_cast<int *&>(_pValue))``, refers to that data
after it, and so does a view parameter assigned another view, ``_Values =
std::span<int>(Local)``, and assigning it anywhere, ``if (_bClear) _pValue = nullptr;``,
changes the pointer, not what it points to. Take the data by value to use it
later, or return a ``TCUnsafeFuture`` when the data is known to outlive the
coroutine.

A template is checked as each of its instantiations in the translation unit
makes it, with the types it has there, like ``f_Read<int *>``, also the
parameters a pack expands to and a class whose base is an actor in it; one
that isn't instantiated isn't checked.

Member functions of actors that are not static can't take references, whatever
their uses: actor calls move the arguments into the coroutine frame once, so
they are reported for any reference parameter, but not for an explicit
object, ``this CApp &_Self``, which is the object, like ``this``. Neither can they use pointers
and views they take, also before they first suspend, also what a template
parameter is instantiated with, `f_Read<int *>`: an actor call starts the
coroutine after the call returns, when what they point to can be gone.
Assigning a pointer parameter, ``_pValue = &m_Value``, doesn't use what it was
given, and neither does what reads it after it is assigned on every path.

A ``TCUnsafeFuture`` is a ``TCFuture``, and what its coroutine borrows is the
responsibility of the code that calls it, also when it gives the future on as
a ``TCFuture``, ``TCFuture<int> fg_Wrap(int &_Value) { return
fg_Unsafe(_Value); }``, which the check doesn't follow.

Pointers and references to classes that derive from ``CActor``,
``CActorInternal`` or ``CAllowUnsafeThis`` are used like ``this``, like the
``CInternal *_pThis`` of a static function, and are not reported.

A function that runs to completion, declared with ``DMibRunsToCompletion``
(see :doc:`malterlib-runs-to-completion <runs-to-completion>`), is joined by
its callers, which keep what its parameters refer to until then, so its
parameters are not reported, unless it is a member function of an actor.

Nothing is reported in a translation unit with an error that stops it from
compiling: its control flow isn't built, and what the errors leave can't be
relied on.

.. example-headers malterlib-runs-to-completion.h malterlib-concurrency-void.h

.. example-check

  TCFuture<int> fg_Suspend();
  struct CInternal : CActorInternal
  {
    int m_Value = 0;
  };
  struct CApp : CActor
  {
    TCFuture<int> f_Read(int const &_Value)  // warning: a reference parameter of an actor's member function
    {
      co_return _Value;
    }
    TCFuture<int> f_Run(int *_pValue)  // warning: a pointer an actor's member function takes
    {
      co_return *_pValue;
    }
  };
  TCFuture<int> fs_Run(CInternal *_pThis)  // fine: the object of an actor is used like this
  {
    co_await fg_Suspend();
    co_return _pThis->m_Value;
  }
  DMibRunsToCompletion TCFuture<int> fg_Joined(int const &_Value)  // fine: joined by its callers
  {
    co_await fg_Suspend();
    co_return _Value;
  }
  TCFuture<int> fg_Later(int const &_Value)  // warning: used after a suspension
  {
    co_await fg_Suspend();
    co_return _Value;
  }

Where what the result keeps can't be told, past what provenance follows, only
what can give it what refers to the data counts, its storage given on as it
is or a value that can hold a pointer, not a number read of it,
``CDescription(_Text.f_GetLen(), ...)``, nor a condition, ``_bFallback ?
fg_Name(g_Config) : nullptr``. A member of a local view, or of a pointer or
view taken by value, assigned on its own replaces what the whole was given,
``CIoSpan Result = _Span; Result.m_pData = g_Output; co_return Result;`` or
``_Span.m_pData = nullptr; co_return _Span;`` (not when it is only assigned
on some paths); a local record that isn't a view carries what is put in
each of its members, so ``CReadResult Result{_Span}; co_return Result;`` is
reported as ``co_return CReadResult{_Span};`` is; a member, also a view, is
assigned also by a member function called on the record whose body is seen
and that no override replaces, by what its flow leaves in that member where
it returns: set on every way through it to what it is given,
``View.f_Set(&Local)`` of ``m_pJob = _pJob;``, also with other members
changed after it, ``m_pJob = _pJob; ++m_nSets;``, on both branches of an
``if``, or through ``(*this).m_pJob``, it replaces what the member held (not
one that sets it on one way only, ``if (_bSet) m_pJob = _pJob;``, which adds
a value, nor one that calls after it what can change it and can't be seen,
``m_pJob = _pJob; f_Log();``). So is a member of a record given by
reference or by a pointer to what isn't const to a function whose body is
seen, by what its flow leaves in that member of what the parameter refers to,
``fg_Set(View, &Local)`` of ``_View.m_pJob = _pJob;``, and what the function
leaves there for each argument is given after all of the call's own changes;
what a pointer parameter points to whose value goes where the flow can't
follow it, ``g_pSaved = _pView;``, or a pointer parameter given another value
first, isn't told that way. A setter that calls itself, or another
that calls it back, sets the member on every way where each way that doesn't
call it again does, ``if (_nDepth == 0) { m_pJob = _pJob; return; }
f_Set(_pJob, _nDepth - 1);``, also one that gives itself, or another that
calls it back, its parameters in another order each time; so does a
function that sets a pointer it is given by reference or by its address and
calls itself. A write through another pointer or reference of the same member
of an object of the same type, ``_pOther->m_pJob = ...``, also through a
member that can point to the object itself, ``m_pSelf->m_pJob = ...``, or of
a whole object of a type that holds the member, ``*_pOther = _Old;``, a base
class or a class derived from one, or a type that holds one by value, can be
a write of the object itself, so what it writes there is what the member can
hold after the call too, unless the parameter is stated distinct
(``DMibDistinct``, which also excludes the object a member function runs
on); so can a write whose place can't be told, through what a function that
can't be seen gives, ``fg_OtherView()->m_pJob = ...``, of an object of such a
type, and one of bytes, through a ``void`` or character pointer, which can be
any object that code that can't be seen reaches: static storage and what a
caller gives whose address escapes to it, ``g_pView = &View;``, also through
a call whose body is seen and keeps it there, ``fg_Register(&View)`` of
``g_pRegistered = _pView;``, also through what it calls in turn, a call of
itself among them, or a member function whose body is seen and keeps its
object there, ``View.f_Register()`` of ``g_pRegistered = this;``, or of one
that can't be seen whose parameter states that it keeps it
(``DMibKeepsArgument``), not a local of the caller whose address doesn't
escape. What such a body keeps is told by what it does with what it is
given, not by what that holds: a pointer read out of the object,
``*((void **)&_View)`` or ``*m_pStack[0]`` of a member that is an array of
pointers, points to another object, which is what it keeps. A write of
another member,
``_pOther->m_pSaved = ...``, or of what can't hold the member, an ``int``,
isn't one of it, nor is one of a member of another object of a type that
holds the member's object by value, which is a member of its own object; a
write of a record that holds a pointer of the member's type by value can be
one of what a pointer parameter set by reference refers to, ``*_pHolder =
_Old;`` of ``CJob const *&o_pJob``; a write of another member of a union the
member is in is, also of a union a record on the way to it is in, ``_pOther->m_B.m_pX = ...`` of
``m_A.m_pJob``, and what the member holds after it can't be told. So is a
write through a pointer member that can point to the member itself,
``*m_ppJob = ...``. A subscript of a
pointer by 0 is what it points to, ``*ppJob``: ``ppJob[0] = _pJob;`` of
``auto ppJob = &m_pJob;`` replaces what ``m_pJob`` held, and so does
``(&View)[0].m_pJob = &_Job;`` of ``View.m_pJob``. An element of a constant
index of an array member, or of an array or a container of the frame, is a
place of its own: ``m_pJobs[0] = _pJob;`` replaces what ``m_pJobs[0]`` held,
also where a setter or a function given the record by reference does it,
and a write of ``m_pJobs[1]``, also through a pointer to it,
``*(m_pJobs + 1) = ...``, doesn't touch it; a write at an index that isn't a
constant, ``m_pJobs[_iJob] = ...``, can be any element, only maybe. What a function stores through a pointer or reference member of its
object is kept where that member points or refers to, by the values the
member holds there, ``m_ppJob = &m_pSaved; *m_ppJob = _pJob;`` in
``m_pSaved``, and where that can't be told in the object as a whole,
``m_rpJob = _pJob;``. What a placement
``new`` gives is where it is placed, and a local a closure refers to by
reference is its function's, so writes through either can be of the object.
What reads only replaced members, also
after a suspension, ``_Span.m_pData = Buffer; co_await fg_Work(); ...
_Span.m_pData``, or ``_Span = {Buffer, _Span.m_nBytes};``, whose length is
only a value, doesn't use what the caller gave. Assigning a pointer or a view
taken by value, ``_Span = {g_Default, 8}``, or a member of such a view,
``_Span.m_pData = nullptr``, writes the frame's copy, not the caller's data. A getter of a view whose
body reads only its own storage may give a member of only values to a
function by reference, ``fg_Min(m_Length, umint(4096))``.

What a reference or pointer parameter refers to is alive where it is used when the context there holds by value a
keeper of it, of an anchor of the use's access path, the expressions around the name whose provenance derives from it,
casts, member accesses, dereferences, subscripts, conditionals and calls whose result is or is into what they are given,
``static_cast<CJob *>(_pContext)->m_Result`` and ``fg_Job(_pContext)->m_Result`` of ``return static_cast<CJob
*>(_pContext);`` of a ``void *_pContext`` anchored by ``CJob``. What the use touches is read off that path: what its
outermost expression designates or refers to, and what each step on it reads, a load the storage it loads, a call the
storage its body reads on the way to what it gives; the address of a pointer parameter is its own storage in the frame,
``fg_Reset(&_pJob)``, and what it points to is touched where its value is read through, or kept by what it is given
to. What a use does to what a reference designates or a pointer points to is told the same way for both, past
parentheses, casts that keep the value, the arms of a conditional, the right of a comma and ``?:``, and into the body
of a function it is given to that is seen: compared or tested, ``(_bEnabled ? _pJob : nullptr) != nullptr``, also by
such a body, ``_pExpected == _pJob`` of a ``TCWeakPointer`` whose ``operator ==`` compares ``fp_GetContained() ==
_pOther``, an address only compared, ``&_Job == _pOther``, and an unevaluated operand, ``sizeof(*_pJob)``, touch
nothing; dereferenced, also through ``->*``, given to a function that isn't seen, returned, or stored in a member or a
global, it is. What a pointer or a reference member of what it refers to points to, also as a getter gives it, ``auto
*pJob = _Slot.f_TryGet();`` of ``return m_pJob;``, is where its class points it when the member states that only its
class points it (``DMibSetByOwnClass``), and can't be told otherwise, so a use of it after a suspension is one of the
parameter. A local reference, a structured binding by reference, the variable of a range-based ``for`` by
reference, a local pointer it is copied into and a capture by a closure called in the scope go on with their names;
a lambda that isn't a coroutine reads its captures where it is called, so a capture by one whose calls all run before
the first suspension, of a function object it is in those its definition reaches, ``TCFunction<int()> fRead =
[&_Job] { ... }; fRead = [] { return 42; };``, uses nothing after it (one nothing calls is held as a local is, unless
each function object it is in is given another value);
a copy of what it designates reads it. A body that gives the value back hands it on, and the caller goes on with the
call's value, ``fg_Const(_pJob) == _pExpected`` or a forwarding template's ``fg_Forward<t_CPointer>(_pJob) != nullptr``;
a name of a local the value was copied into counts only where the value still reaches it, so after ``pCurrent =
&g_DefaultJob;`` a use of ``pCurrent`` isn't one; a pointer converted to an integer is its value: returning, logging or
storing the integer, ``reinterpret_cast<umint>(_pJob)``, uses nothing, converting it back to a pointer gives the pointer,
and a local integer it is put in holds it only where that value still reaches, not after ``Address =
reinterpret_cast<umint>(&Local);``; a holder of the frame, a local record, a pointer or an integer, uses it after a
suspension only where it is used after one, or where its destructor runs after one, at the end of its scope:
``{ CReadScope Scope{_pValue}; } co_await fg_Work();`` uses nothing after the suspension; a body the value is given to at a level its
parameter's type doesn't have, ``&_pState`` given to a ``void *``, is taken as one that can't be seen. A constructor
is a call, its body followed where it is seen; the object of a member function whose body is seen is followed into
that body, which reads only what it reads, and one that gives back its object, ``return *this;``, hands it on,
``&_pJob->f_Self() == _pOther``. What a body that can't be seen gives back, or makes, goes on where its type can refer
to it. The walk reports a use only where it reaches one; any other end is over. By type as reference capture tells
(:doc:`coroutine-reference-capture`), in the context where the use is written, the function executing it, whose frame holds the parameters themselves, also of a coroutine lambda, ``_pJob`` of ``[](CJob *_pJob, TCSharedPointer<CJob> _pKeep) -> TCFuture<int>``, whether it is called where it is made or later: its parameters held by value, the explicit object too, and the locals of its body held by value in scope at the use, declared before it in a scope still open there (of a value kept in a local of the frame, read where that local is, each whose scope is open at the use), each by its declared type, ``TCFuture<int> fg_Read(CJob const &_Job, TCSharedPointer<CJob> _pKeep)``; of a generic lambda, those of the specialization that runs; through a lambda that isn't a coroutine, called where it is made or later, ``auto fRead = [&_Job] { return _Job.m_Result; };``, what it holds and what the code around it holds, out to the nearest coroutine; through a coroutine lambda, only what that holds; also of a member function of an actor that takes a pointer, its parameters held by value, which an actor call takes with it. A keeper fits an anchor of the borrow, every object type on its derivation, past casts, getters, also of an explicit object, and local pointers and references: of a use after a suspension the use itself, so ``_pJob->m_Value`` of a ``TCSharedPointer<CJob> const &_pJob`` reads the caller's owner and needs a keeper of that; of a use before it that keeps a reference, the whole expression it is in, ``int *pValue = &_pJob->m_Value;`` of what ``_pJob`` owns after ``auto pKeep = _pJob;``. A value kept is asked about where it is kept: in a capture, by what that closure holds, ``[pValue = &_pJob->m_Result, pKeep = _pJob]``; in a local, by the frame; in the result, by the result. A ``buffer`` or ``sharedbuffer`` keeper fits a borrow of what an owner of its own type owns, ``CStr Keep = _Text;`` before ``_Text.f_GetStr()``. Such a use is neither one after a
suspension nor one that keeps it, unless the result keeps it; the result is
judged by the type the promise stores, the ``T`` of the ``TCFuture<T>``, not
by what ``co_return`` gives it: one that can't hold a pointer, a reference or
a view keeps nothing, ``co_return _Text;`` of a ``CStrSpan`` in a
``TCFuture<CStr>``. Else it is checked by itself, a new value or moved, also ``co_return Result;`` of a local view, which moves it, while a local record that isn't a view carries what is put in each of its members, so ``CReadContext Context{&_Value}; co_return Context;`` is reported as ``co_return CReadContext{&_Value};`` is, and the value of a pointer, a reference or a view member stored in a record that isn't a view, an owner nor an actor, reached through a pointer or a reference to the record, is a value whose members aren't followed, ``co_return pContext->m_pValue;`` of ``auto *pContext = &Context;``, also as a getter gives it, ``auto *pJob = _Context.f_Job();`` of ``return m_pJob;`` and ``_pJob.f_Get()`` of a ``TCPointer``, also a function given the record by pointer or by reference alike, ``auto *pJob = fg_Job(_pContext);`` of ``return _pContext->m_pJob;``, and ``co_return _Job;`` of another type than ``T``, which makes a new ``T`` whatever its category, and keeps alive what it holds a keeper of by value, of
what is borrowed whatever it is, an owner's, the object or what a parameter
refers to, ``co_return CReadResult{_pKeep, &_Job.m_Result};``; the keeper's own initializer, ``_pKeep``, is not on the derivation of what it keeps, so an owner of an unrelated type put in is still reported. The promise is
plumbing, never looked into: ``co_return X`` initializes the ``T`` from ``X``,
judged by type. Of the type ``T`` it is a copy or a move of ``X``, which holds
what its parts refer to; of another type it is a conversion that can't be seen,
which, when ``T`` can hold a pointer, a reference or a view, can keep what the
parts of ``X`` refer to and a reference to an ``X`` given as an lvalue,
whichever constructor would be selected, ``co_return _Value;`` into a
``TCFuture<CKeepsRef>``, but not to one given as an rvalue,
``co_return fg_Move(_Value);``. What a
function or a constructor keeps of what it is given is told by its body, or
of one whose body can't be seen by type, also through a pointer to a
function or a member function or any other callee, by its function type:
only its result, the object it makes or is called on by a member function
that isn't const, and what its other arguments given by reference or pointer
to what isn't const refer to can keep it, each when its type can hold a
pointer, a reference or a view; a const call operator of a function object
keeps nothing in it; an owner whose copy keeps what it owns, a weak owner and
a handle given by reference are copied by a body that can't be seen, while
one that can be seen tells what it keeps of the owner, ``static
TCSharedPointer<CJob> const *s_pValue; s_pValue = &_Value;`` keeps it; a
constructor whose parameter states that it keeps it (``DMibKeepsArgument``)
keeps it in the object it makes as a whole, whatever its members are, which
uses it where it is used or destroyed, ``CPointerGuard Guard(_pValue);`` of
``CPointerGuard(DMibKeepsArgument int *)`` with ``~CPointerGuard()`` before
a suspension is reported; a keeper made of a parameter that states it is
distinct from the others (``DMibDistinct``) doesn't keep what another
parameter refers to or owns, ``co_return CReadResult{_pOther,
&_pJob->m_Value};`` of ``DMibDistinct TCSharedPointer<CJob> const
&_pOther`` is reported, and keeps it by type without; what is given as a prvalue or an
xvalue is moved or copied, never referred to, and of a type that can't hold a
pointer, a reference or a view nothing of it is kept, ``fg_Hold(fg_Move(_Value))``.
So ``CConnection
Connection(_pJob);`` of a shared owner, ``fg_Inspect(_Value);`` and
``_fInspect(_Value);`` through a ``void (*)(CJob const &)`` returning
``void``, ``Sink.f_Read(_Value)`` of a const member function and ``CValues
Values(_Value);`` of a class of values keep nothing. What a call keeps in
what another argument refers to or points to, a local of the frame whose
address doesn't escape, a pointer as well as a record, ``fg_Keep(&_Job,
&pHeld)`` of ``*o_ppJob = _pJob;`` or ``&Holder.m_pJob``, the frame holds
there, so it is a use after a suspension only where that local is read after
one. What a body keeps in what a closure it makes and calls writes, through
``this`` or a pointer it captures, is where the closure writes it; past 64
such closures, where it keeps it is told by type. A member a constructor's
initializer makes as an aggregate holds what the body then gives a part of
it too, and an initializer's value is what a member holds after the
constructor only where the body doesn't give it another.
Each object it is kept in is then followed by the same rules: one a member
function keeps it in, also a member of its own object, ``m_Text = _Text``, is
a temporary of its full expression that ends there unless the expression
gives it on, ``CText Copy = CFormat("{}") << _Text;`` into a ``CText`` that
can't hold a view keeps nothing, while a named view keeps it, and so does a
copy or move of a view, ``CFormatStrSpan Format = CFormatStrSpan("{}") <<
_Text;``; a record held by value in the frame that isn't a view, an owner nor
an actor carries it in the member it is put in, by an aggregate, ``CPair
Pair{_pJob, 1};``, an assignment to the member or a copy or move assignment
the compiler defines, ``Pair = CPair{_pJob, 1};``, and a later load of that
member is it again, ``Pair.m_pJob->m_Result``, also through a copy the
compiler defines, a structured binding or a getter whose body is seen; a
constructor, seen or not, ``CStored Stored(_pJob);``, a member function or
another function that keeps it keeps it in the record as a whole, ``CHolder
Holder(_Value);``,
``Holder.f_Keep(_pJob)``, ``fg_Store(_Value, Sink)``, ``auto Held =
fg_Hold(_Value);``. The record uses it where it is used or destroyed: a call
on it, or a destructor that isn't trivial (seen, its body; not seen, it uses
what the record holds); one trivially destroyed and never used again uses
nothing of it. A copy of a record the walk starts at, whose members' values
can't be told, is a value, ``CReadOptions Options = _Options;``, and so is a
copy a constructor written by hand makes.
What a body keeps in static storage is kept past every call. A body that keeps it in what
another of its parameters refers to keeps it in that argument,
``fs_Add(*this, _Text)`` in the object, so ``CStr Copy = "{}"_f << _Text;``
keeps nothing past the statement.
A keeper keeps what an owner owns only when it owns the same object where it
is taken, as reference capture tells (:doc:`coroutine-reference-capture`).
Five misses are known: an owner released or reassigned between the borrow
and the use where it isn't in a local slot, a keeper that owns a different
object of a fitting type where the provenance can't tell, a record that is a
member of the object given the address of what a parameter refers to,
``m_Info.m_pValue = &_Value; co_return m_Info;``, an awaiter whose type can't hold a pointer, a reference or a view (list its type in ``ViewTypes`` to have it checked), and what a function or
constructor whose body can't be seen keeps elsewhere, a static or a
registry, aren't reported. A member of a record read through a pointer to the
record, a record parameter held by value given such an address, and a copy of
a record that a closure captures by value and reads the member of, are
reported.

.. example-check
   struct CJob { int m_Value = 0; };
   struct CTwo { CJob const *m_pJob; int m_Count; };
   struct CInfo { int const *m_pValue; int m_Count; };
   struct CHolder { TCSharedPointer<CJob> m_pJob; };
   struct CResult { TCSharedPointer<CJob> m_pJob; int const *m_pValue; };
   struct CIdAwaiter
   {
     CIdAwaiter(int const *_pValue);
     bool await_ready();
     void await_suspend(std::coroutine_handle<> _Handle);
     int await_resume();
     unsigned long m_Id;
   };
   TCSharedPointer<CJob> fg_NewJob();
   void fg_Register(int const *_pValue);
   TCFuture<int> fg_Suspend();
   TCFuture<int> fg_ReadThroughPointer(CJob const &_Job)  // warning
   {
     CTwo Two{&_Job, 1};
     auto *pTwo = &Two;
     co_await fg_Suspend();
     co_return pTwo->m_pJob->m_Value;
   }
   TCFuture<CInfo> fg_RecordParameter(int const &_Value, CInfo _Info)  // warning: a parameter held by value
   {
     _Info.m_pValue = &_Value;
     co_return _Info;
   }
   TCFuture<int> fg_Captured(int const &_Value)  // warning: a record a closure captures by value
   {
     CInfo Info{&_Value, 1};
     auto fRead = [Info] { return *Info.m_pValue; };
     co_await fg_Suspend();
     co_return fRead();
   }
   TCFuture<int> fg_CapturedCount(int const &_Value)  // fine: the record's own part
   {
     CInfo Info{&_Value, 1};
     auto fRead = [Info] { return Info.m_Count; };
     co_await fg_Suspend();
     co_return fRead();
   }
   TCFuture<int> fg_Reassigned(CHolder *_pHolder)  // clean: a known miss, an owner reassigned between the borrow and the use
   {
     auto pKeep = _pHolder->m_pJob;
     int const &Value = _pHolder->m_pJob->m_Value;
     _pHolder->m_pJob = fg_NewJob();
     auto pOther = _pHolder->m_pJob;
     co_await fg_Suspend();
     co_return Value;
   }
   TCFuture<CResult> fg_OtherKeeper(TCSharedPointer<CJob> const &_pFirst, TCSharedPointer<CJob> const &_pSecond)  // clean: a known miss, a keeper of another object
   {
     co_return CResult{_pFirst, &_pSecond->m_Value};
   }
   struct CApp
   {
     CInfo m_Info;
     TCFuture<CInfo> f_Member(int const &_Value)  // clean: a known miss, a record that is a member of the object
     {
       m_Info.m_pValue = &_Value;
       co_return m_Info;
     }
   };
   TCFuture<int> fg_Awaiter(int const &_Value)  // clean: a known miss, an awaiter whose type can't hold a pointer
   {
     co_return co_await CIdAwaiter(&_Value);
   }
   TCFuture<int> fg_Unseen(int const &_Value)  // clean: a known miss, kept elsewhere by a body that can't be seen
   {
     fg_Register(&_Value);
     co_await fg_Suspend();
     co_return 0;
   }

A local record carries what is put in its members, also by a setter whose
body is seen. Each member is a place of its own however the record is
reached: a constructor whose body is seen keeps each argument only in the
members it initializes or assigns with it, ``CReadContext(int *_pValue) :
m_pInput(_pValue), m_pFallback(&g_Default)`` keeps nothing of ``_pValue`` in
``m_pFallback``; a local reference to the record is the record, with the same
members, and an assignment to a member through it replaces what the member
held; a pointer to the record designates it, and a setter whose body is seen
called through it, ``pView->f_Set(&Local)``, sets the member as one called on
the record does; a copy copies member by member. What a member holds at a
point is what the definitions of that member that reach there give, and each
use asks that the same way: a read of the member; a member function called on
the record, ``Hold.f_Get()``, which reads what the member holds at the call;
and the destructor where the scope ends, which uses what the members it reads
hold there, so ``Slot.m_pValue = &g_Value;`` or ``Scope.m_pValue = nullptr;``
before the suspension leaves nothing of the parameter for it. A setter whose
body calls a setter of the object that assigns on every way through it assigns
on every way, ``f_Set`` calling ``fp_Set``, as its flow tells; a constructor whose body is seen
and makes a member as an aggregate, ``m_Read{_pInput, _pFallback}``, keeps each
argument only in the member of it that it initializes; an element of a nested
list, ``CNested Hold{{_pJob, &g_OtherJob}}``, is put in that member of the
nested record; and a copy or a move assignment the compiler defines gives each
member what the record copied gives it. A function whose body is seen
and that assigns through a pointer parameter on every way through it,
``*o_ppJob = _pJob;``, gives what the argument points to that value, as one
through a reference parameter does, also on both branches of an ``if``,
through a cast, ``o_pJob = static_cast<CJob const *>(_pJob);``, through a
local reference to it, or through another such function it calls; one that
assigns on some ways only adds a value:

.. example-before
   struct CJob { int m_Value = 0; };
   struct CReadContext { int const *m_pValue; };
   struct CJobView
   {
     CJob const *m_pJob;
     void f_Set(CJob const *_pJob) { m_pJob = _pJob; }
   };
   TCFuture<int> fg_Suspend();
   CJob g_OtherJob;

.. code-block:: c++

  TCFuture<CReadContext> fg_Read(int const &_Value)  // warning
  {
    CReadContext Context{&_Value};
    co_return Context;
  }

  TCFuture<int> fg_ReadLocal(CJob const &_Job)  // fine
  {
    CJob Local;
    CJobView View{&_Job};
    View.f_Set(&Local);
    co_await fg_Suspend();
    co_return View.m_pJob->m_Value;
  }

  TCFuture<int> fg_ReadThroughPointer(CJob const &_Job)  // fine
  {
    CJob Local;
    CJobView View{&_Job};
    auto *pView = &View;
    pView->f_Set(&Local);
    co_await fg_Suspend();
    co_return View.m_pJob->m_Value;
  }

  struct CTwo
  {
    CTwo(CJob const *_pJob) : m_pJob(_pJob), m_pOther(&g_OtherJob) {}
    CJob const *m_pJob;
    CJob const *m_pOther;
  };

  TCFuture<int> fg_ReadOther(CJob const &_Job)  // fine
  {
    CTwo Two(&_Job);
    auto &Alias = Two;
    co_await fg_Suspend();
    co_return Alias.m_pOther->m_Value;
  }

  TCFuture<int> fg_ReadMember(CJob const &_Job)  // warning
  {
    CTwo Two(&_Job);
    auto &Alias = Two;
    co_await fg_Suspend();
    co_return Alias.m_pJob->m_Value;
  }

  void fg_SetJob(CJob const **o_ppJob, CJob const *_pJob) { *o_ppJob = _pJob; }

  TCFuture<int> fg_ReadSet(CJob const *_pJob)  // fine
  {
    CJob Local;
    fg_SetJob(&_pJob, &Local);
    co_await fg_Suspend();
    co_return _pJob->m_Value;
  }

  struct CWriteBack
  {
    int *m_pValue;
    ~CWriteBack() { ++*m_pValue; }
  };
  int g_Value = 0;

  TCFuture<int> fg_WriteBack(int &_Value)  // fine
  {
    CWriteBack Slot{&_Value};
    Slot.m_pValue = &g_Value;
    co_await fg_Suspend();
    co_return 0;
  }

  TCFuture<int> fg_WriteBackKept(int &_Value)  // warning
  {
    CWriteBack Slot{&_Value};
    co_await fg_Suspend();
    co_return 0;
  }

  struct CForwardingView
  {
    CJob const *m_pJob;
    void fp_Set(CJob const *_pJob) { m_pJob = _pJob; }
    void f_Set(CJob const *_pJob) { fp_Set(_pJob); }
  };

  TCFuture<int> fg_ReadForwarded(CJob const &_Job)  // fine
  {
    CForwardingView View{&_Job};
    View.f_Set(&g_OtherJob);
    co_await fg_Suspend();
    co_return View.m_pJob->m_Value;
  }

  TCFuture<int> fg_ReadNotForwarded(CJob const &_Job)  // warning
  {
    CForwardingView View{&_Job};
    co_await fg_Suspend();
    co_return View.m_pJob->m_Value;
  }

Options
-------

.. option:: OwnerTypes

   Semicolon-separated list of owners, each `Type=kind`, a list the checks
   share (:doc:`/clang-tidy/MalterlibOptionsFile`): those of the `buffer`
   and `sharedbuffer` kinds own the data they hold in a buffer on the heap,
   which made of anything other than one of their own copy it, like `CStr
   Text(_Text)` of a `CStrPtr`. Built in is `std::vector=buffer`.

.. option:: ViewTypes

   Semicolon-separated list of types and aliases that refer to data. A leading
   `*` matches the end of a name, a trailing one its start, and qualified
   names match qualified names. A template followed by a pattern of its first
   argument, `TCStr<CStrTraitsPtr_*>`, matches its specializations with such
   an argument, which is what a parameter whose type is deduced from an alias
   like `CStrPtr` is.
   Built in are `std::span;std::basic_string_view;std::initializer_list`, and the Malterlib options file (:doc:`/clang-tidy/MalterlibOptionsFile`) adds Malterlib's, like `*StrPtr` and `TCStr<CStrTraitsPtr_*>`, for every check that looks at views.

.. option:: CheckPointers

   Whether pointer parameters are reported. Default is `true`.
