.. title:: clang-tidy - malterlib-coroutine-actor-reference

malterlib-coroutine-actor-reference
===================================

Finds pointers and references into the data of an actor that a coroutine of
the actor looks up, and then uses after a suspension. The coroutine is
cancelled if the actor is destroyed, but other calls of the actor run during
the suspension and can change or erase the data. This is a best effort check.

.. code-block:: c++

  TCFuture<void> f_Process(CStr _Key)
  {
    CValue *pValue = mp_Items.f_FindEqual(_Key);
    co_await f_Other();
    co_return pValue->f_Use();  // warning
  }

A lookup is a pointer or reference initialized with a member function call,
subscript or call operator on the data of ``this``, also a static local or a
pointer parameter assigned one, like
``mp_Items.f_FindEqual(_Key)``, ``mp_Items[_Key]`` or ``*mp_Items(_Key)``,
which finds or adds an entry of a ``TCMap``, also through the bits of a
pointer, ``fg_BitCast<int *>(&mp_Items[0])``. A member function of the actor
itself whose body can be seen, and that isn't overridden, gives what it
returns: a lookup only when that is one, not a member, ``return m_Value;``, or
what it is given, ``return _pValue;``, unless that is one. A coroutine that gets the
object of an actor through a parameter, like ``CInternal *_pThis``, also a
member of another actor, looks up its data the same way, like
``_pThis->mp_Items(_Key)``, also through such a member function,
``_pThis->f_Value()``. Dereferencing
an owning pointer member,
like ``*mp_pInternal``, also with the operator called by name,
``mp_pInternal.operator*()``, gives data that lives as long as the actor, which
doesn't replace it while it runs, and is not a lookup; the value of a pointer
member of an entry, ``int *pValue = mp_Items[Key].m_pValue``, points to an
object of its own, which the entry being kept doesn't keep, and so does a
value that a subscript or call operator gives, like ``int const &Value =
mp_Values[1]`` of ``int operator[](int) const``. A member reached through a
pointer to it, ``(this->*pmItems)[Key]``, is in the object.

Look the data up again after the suspension, or re-validate the pointer on
every resume with ``fg_OnResume``. A pointer or reference into the data is
re-validated by such a function when it looks it up again, also in a lambda
it calls where it is that captures nothing by copy, ``[&] { pEntry =
mp_Items.f_FindEqual(Key); }()``, before what can leave that, also by giving it
to what can assign it, also a virtual function that isn't final, whose
override can, (not to a function whose body is in the file and
doesn't assign it, in a statement of its body, what isn't only another of
its parameters, like ``void fg_NoOp(int *&_pEntry) {}`` or one that puts back
what it is given, ``_pEntry = _pSaved``, also by giving it to another that
does after it looks it up, ``fg_Restore(_pEntry, _pSaved)``, also the address
of what it is given
by reference, ``_pEntry = &_Value``, also cast, or does so on some paths
only, also after what can return first, ``if (_pEntry) return;``), on
every path (not by
a callback that removes entries of the collection it looks up in,
``mp_Items.f_Clear()``, also in a scope exit it makes, ``g_OnScopeExit /
[this] { mp_Items.f_Clear(); }``, but not in a branch a constant condition
never takes, also through a helper it gives the collection to, also through
a local reference to it there, also through a member function of the actor it calls,
``f_Tidy()``, also through an unchanged pointer to it, ``pThis->f_Tidy()``,
and any override of a virtual one, or a lambda it calls, or registered before one alive at the
resume that does, which runs after it on resume; and of a registration made
in either value of a conditional, only what both re-validate, nothing for a
guard, ``bChoose ? co_await fg_OnResume(...) : co_await
f_CheckDestroyedOnResume()``), or
compares it with a fresh lookup, like
``pEntry != mp_Items.f_FindEqual(Key)``, and acts on the result (in a callback
that returns an error, one that it returns or leaves with when they don't match,
not ``p == pFresh || !pFresh ? CExceptionPointer{} : ...``, which goes on when
the entry is gone, nor ``p == pFresh ? fg_MakeError(...) : ...``), in a
condition or returned, also through a local it is saved in that isn't changed
after, ``bSame = true``, or a call it is
given to, not one that only logs it, ``fg_Log(pEntry != ...)``: not by assigning
it a pointer kept from before, like ``pEntry = pSaved``, also through a cast to
a reference, ``static_cast<int *&>(pEntry) = pSaved``, (a lookup assigned
through a cast to a reference, ``static_cast<int *&>(pEntry) = ...``, is one
too, also through any number of local references to it, but not one assigned
as either of a conditional, ``(bFirst ? pFirst : pSecond) = ...``, also through
a reference bound to one, ``int *&pAlias = bChoose ? pFirst : pSecond``, which
may keep what it held) also through a cast or
arithmetic, ``static_cast<int *>(pSaved)`` or ``pSaved + 0``, moved,
``fg_Move(pSaved)``, or given back by a helper, ``fg_Identity(pSaved)``, nor by only reading it, like
``*pEntry == 0``, or testing it for null, like ``if (!pEntry)``, also of an
iterator or a reference handle, ``if (!iEntry)``, which don't
tell that its entry is still there. A lookup in a block of the callback, ``{
pEntry = ...; }``, runs each time it does. Taking the address of a stale
pointer, ``&pEntry``, or binding a reference to it, ``int *&pAlias = pEntry``,
doesn't read it; what is read through those is. A lambda dispatched after a suspension,
like ``g_Dispatch / [pEntry] { ... }``, uses what it captures, and since it
runs later, what it assigns, ``[&pEntry] { pEntry = &mp_Items[0]; }``,
doesn't look the pointer up again where the coroutine reads it after. The
pointer a subscript indexes is looked up where it is written, before an index
written after it that suspends, ``(&mp_Items[0])[(co_await fg_Suspend(),
0)]``, and after one written before it, ``(co_await fg_Suspend(),
0)[&mp_Items[0]]``. What a
pointer member of an entry points to, like ``*Entry.m_pValue``, also
through a local made from the member, like ``auto &pValue = Entry.m_pValue``,
also copied through a reference to the member, ``int *pValue = pMember``,
is an object of its own, which no phase of the collection keeps. What
``f_Get()`` or ``get()`` of an owning pointer member gives, a class with
``operator->``, lives with the actor (not what that of another member gives,
like ``int *f_Get()`` of a vector it can clear), but what one in an entry gives,
``mp_Items[Key].m_pValue.f_Get()``, dies with the entry, and what an optional
member holds, ``*mp_Value`` of a ``TCOptional``, can be cleared, while an
element of an array the actor holds, ``mp_Values[Index]``, also of a
``std::array``, also through its accessors, ``mp_Values.data()`` or
``mp_Values.front()``, lives with the actor. A reference bound to a temporary copy,
like ``unsigned const &Bits = mp_Items[1].m_Bits`` of a bit-field, refers to
the copy; one cast to a reference, ``static_cast<int &>(mp_Items[1])``, or
through a pointer to a member, ``mp_Items[1].*pmValue``, refers into the
entry. An iterator that is
moved on, like ``++iEntry`` or ``iEntry.f_Next()``, also through a
reference, is at another entry. What is made from a pointer, like ``auto &Entry
= *pEntry``, is re-validated with it only when the callback compares the
pointer with a fresh lookup, like ``pEntry != mp_Items.f_FindEqual(Key)`` or a
pointer the callback assigns one to, and leaves with an error when they don't
match, before it can return success; testing it for null, or comparing it
with another pointer kept from before, also cast or moved along,
``static_cast<CValue *>(pSaved)``, doesn't tell that the entry it points
to is still there, and the pointer itself isn't re-validated by that
comparison either. After the registration ends, a pointer that the callback
assigns points where the callback may have pointed it, or, when a path of the
callback doesn't assign it, where it pointed before.

A handle that refers to the entry it found, like the ``TCMapResult`` that
``mp_Items(_Key)`` returns, is a lookup too: what is made from it, like
``auto &Entry = *Mapping``, and uses of what it refers to, like ``*Mapping``
or ``Mapping.f_GetResult()``, are checked, and so are the pointers and
references an iterator gives, ``iEntry.operator->()``, ``iEntry.f_GetCurrent()``
or its conversion. Asking whether the lookup added
the entry, ``Mapping.f_WasCreated()``, isn't a use, nor is naming a static
member through a reference or pointer, ``Entry.ms_Count`` or
``pEntry->fs_Count()``, nor one of an entry looked up, ``int const &Value =
mp_Items[0].c_Value``; its other member functions
may read the entry and are. A handle is a class with a reference member,
also an aggregate made from a lookup, like ``CHandle Handle{*pEntry}``.

An iterator into the data of the actor, like ``auto iEntry =
mp_Items.f_GetIterator()``, also assigned one with its ``operator=``, like
``iEntry = mp_Items.f_GetIterator()``, which looks the data up again, is a
lookup too, and so is pointer arithmetic on one, like ``pEntries + Index``. A range-based ``for`` loop over the data of
the actor, like
``for (auto &Entry : mp_Items)``, that suspends in its body and then goes on to
the next entry uses an iterator that other calls of the actor may have
invalidated. It is reported at the range, also when the loop copies the
entries, but not over an array member, ``int m_Values[2]``, or a
``std::array``, which can't lose them. Loop over a copy, or collect what to do before suspending.

Each variable is reported once, at its first use after a suspension. Testing
whether a pointer is null, or comparing it, isn't a use, but through a
reference to a pointer in the entry, ``auto const &pValue = mp_Items[0]``, it
reads the entry. A flag of the entry
that tells whether it was removed, like ``if (*pDeleted) co_return;`` after
the suspension, where ``pDeleted`` was copied from the entry's
``m_pDeleted`` before, validates the data the flag belongs to, not what a pointer of it points
to, like ``pValue`` copied from ``Entry.m_pValue``; the member
must be named for removal (``Destroyed``, ``Deleted`` or ``Removed``), and
the entry not changed since, nor the pointer pointed at another entry, and the condition not suspending after reading
it, nor calling what can remove it, ``*pDeleted || (f_Stop(), false)``; so does an ``fg_OnResume`` callback that tests the flag that way, without
changing its copy, and returns an error. Comparing the flag to be set, like
``*pDeleted == true``, tests it too. Only a flag tested as such, alone or
in a disjunction, counts: ``if (!*pDeleted)`` leaves when the entry is there.
A call after the test that can change the data of the actor, a member function
that isn't const of it, also through a reference to it, ``auto &Items =
mp_Items; Items.f_Clear()``, or a function given it by a reference or pointer
that isn't to const, ``fg_Clear(mp_Items)``, can remove the entry again, when
it can free
memory or end the life of an object, also through what it calls: one that only
links, like removing from an intrusive container, can't, and one that deletes,
or calls what can't be followed, like a function without a body, through a
pointer or a virtual function that isn't final, can.
In a lambda, a use in the branch of ``if (!*pDeleted)``, also with others,
``!*pDeleted && bGo``, in the other branch of ``if (*pDeleted)``, or after
``if (*pDeleted || bSkip) return;``, is of an entry that is there, when the
condition doesn't call what can remove it either, and nothing between the test
and the use calls what uses the actor, like ``mp_Items.f_Clear()``, other than
its const member functions, or a call the use is given to, which runs after
it.

The address of a lookup through ``std::addressof(mp_Items[Key])`` is a lookup
too, and so is an element through one, ``pEntry[0]`` like ``*pEntry``, also
of one assigned before, and so is a lookup moved, ``fg_Move(pEntry)``, or assigned, ``pSecond =
(pFirst = mp_Items.f_FindEqual(Key))``, an element of an array in an entry,
``mp_Items[1].m_Values[0]``, an entry offset, ``&mp_Items[1] + 0``, and an
iterator from a
free ``begin(mp_Items)``, also a pointer
into the data one gives (but not of a fixed array of the actor, or a
``std::array``, whose elements live with it), and either value of a conditional of the same
collection, ``bFirst ? mp_Items[1] : mp_Items[2]``, or the last of a comma, is
a lookup of it, but not a value a
member function gives, like a count, also bound to a reference that extends
it. A member function of what a variable refers to, called after an await in
its arguments, ``Entry.f_Update(co_await fg_Work())``, uses it after the
suspension, and so does what a pointer points to accessed after an await in
the same expression, ``pEntry[(co_await fg_Work(), 0)]``, also through ``->``
or a cast of the pointer, ``(pEntry + co_await fg_Offset())->m_Value``, also in the
pointer, ``(pEntry + (co_await fg_Work(), 0))[0]``, and a call that is
given it by reference or pointer,
``fg_Read(Entry, co_await fg_Work())``, also through a pointer to a
function, ``fUse(Entry, co_await fg_Work())``, or to a constructor, ``CCopy{Entry,
co_await fg_Work()}`` (but not one braced after the last argument that
suspends, ``CCopy{co_await fg_Work(), Entry}``, which is evaluated after it); one made on a lookup as it is,
``mp_Items[0].f_Read(co_await fg_Work())`` (but not when a constant
condition skips the await, ``false ? co_await fg_Work() : 1``), also a pointer from one that is
indexed, ``(&mp_Items[0])[(co_await fg_Work(), 0)]``, is reported at the call (not into
a collection of the own actor declared to lose no entries,
``DMibActorEntries()``, or only in the phases the guards in scope before the call
guard and a callback registered after them can't start, of a type whose entries stay where they are, nor in a
discarded branch or other code that can't run, like after a ``co_return``), but not
the left side of an assignment, which is evaluated after its right side,
``mp_Items[0] = co_await fg_Work()``, which is the second parameter of an
assignment operator with an explicit object, ``operator=(this CValue &_Self,
int const &_Value)``. A swap,
``fg_Swap(pEntry, pOther)``, also ``std::ranges::swap``, exchanges what the
pointers hold, also through a
cast to a reference, ``fg_Swap(static_cast<int *&>(pEntry), pOther)``, a free function
given the collection by reference, ``fg_FindEqual(mp_Items, Key)``, looks up in
it like the member function does, a free function given a lookup for a
reference of the same type, ``fg_Const(pEntry)``, passes the lookup through,
and the address
of a local pointer, ``&pEntry``, is of the local, not of the data, which is
read through both, ``**ppEntry`` of ``int **ppEntry = &pEntry``, also through
more of them, ``***pppEntry``, where it is used as what the local holds
there, and assigning through it, ``*ppEntry = &Local``, assigns the local
where it is written. A comparison with a fresh lookup re-validates only when acting on it
can end the coroutine, like an ``if`` that returns an error, not one that
only logs, and a removal flag tested in a lambda with a ``goto`` doesn't
count, since the jump can pass the test.

A structured binding of a lookup, like ``auto &[Value] = mp_Items[Key]``, and
a lookup that a comma gives, like ``(f_Prepare(), mp_Items.f_FindEqual(Key))``,
are lookups too. A lambda that runs, called, run by ``fg_CallSafe(fRead)`` or
``std::invoke(fRead)``, also by another lambda that runs (but
not what it doesn't evaluate, ``sizeof(*pEntry)``, nor a branch of an ``if
constexpr`` that is discarded), as a scope exit
like ``g_OnScopeExit / [pEntry] { ... }`` where its holder is destroyed, or
where it is made when it is a discarded temporary, ``(void)(g_OnScopeExit /
[pEntry] { ... })``,
unless cancelled by ``f_Clear()`` or run early, ``Cleanup()``, before (which
uses them there, and starts the phases it starts), or as
the cleanup of ``fg_AsyncDestroy`` where its guard is destroyed or awaited
early, ``co_await fg_Move(Cleanup)``, uses the
pointers it captures, and those of the lambdas it calls in turn, like
``fRead()`` in ``[&fRead] { return fRead(); }``, however many, in the order
they run, what each assigns after what those it calls read, unless it only tests them, like ``[pEntry] { return
pEntry != nullptr; }``, or assigns them, ``pEntry = nullptr``, or uses them only after it tested a removal flag
copied from the entry, like ``if (*pDeleted) return;`` or in the branch of
``if (!*pDeleted)``. What a lambda that is called assigns to what it
captures by reference, ``[&pEntry] { pEntry = mp_Items.f_FindEqual(Key); }``,
also a coroutine lambda that ``fg_CallSafe`` runs right away, and what
lambdas it calls in turn assign, is a lookup after the call: for sure when a
statement of its body that runs each time it does assigns it, not in a
branch, a loop, after what can return, or, where what it throws is caught,
``try { fRefresh(); } catch (...) {}``, after a call that can throw, and not
through a lambda it calls only on some paths, ``if (bRefresh) fRefresh()``.
A lambda that looks up again what it captures by copy before it reads it,
``[this, pEntry]() mutable { pEntry = mp_Items.f_FindEqual(1); return
pEntry->m_Value; }``, reads that. A comparison that only re-validates when another
condition holds, like ``bCheck && pEntry != mp_Items.f_FindEqual(Key)``,
doesn't, unless that condition tests that a pointer kept from before is set,
like ``pInitial && pEntry != pInitial``, not a fresh lookup, which is null
when the entry is gone, and so does one in a value of a conditional, unless
the other value is an error, ``bCheck ? (pEntry == ... ? CExceptionPointer{}
: fg_MakeError(...)) : fg_MakeError(...)``, also through helpers that return
one, however many, also through an unchanged local,
``auto Error = fg_MakeError(...); return Error;``, also
``std::make_exception_ptr(...)``, also locals made of others,
each told once, and the value of a conditional that a constant condition
selects, ``false ? Error : fg_MakeError(...)``, and a ``TCAsyncResult``
that is set on every path before it is returned, and not cleared or changed
otherwise after (what changes it before it is set, or in a branch of an ``if
constexpr`` that is discarded, doesn't count), ends the coroutine like an
error. An error in both branches of an ``if`` leaves with an
error. A callback re-validates only what it looks up again on every path that
returns without an error, not after ``if (bSkip) return nullptr;``, and what
it uses before it does, like ``pEntry->m_Value`` before ``pEntry =
mp_Items.f_FindEqual(Key)``, and what was read before the suspension, like
the object of a call whose arguments suspend, ``pEntry->f_Read(co_await
fg_Work())``, isn't looked up again, and is used after the suspension when it runs on
resume, and so is what it reads through and doesn't re-validate at all,
``fg_Read(*pEntry)``, unless a guard of a phase alive before it protects it,
or a callback that compares it with a fresh lookup and leaves with an error
when they differ, and doesn't assign it, ``if (pEntry !=
mp_Items.f_FindEqual(Key)) return fg_MakeError(...)``, which leaves the
value read before as it is.

Pointers into the data of the actor that are held inside other objects, like a
pointer member of a struct that is passed by value, or the elements of an
array, ``int *p[] = {mp_Items.f_FindEqual(1)}``, are not followed, and
neither is a pointer converted to an integer and back,
``reinterpret_cast<int *>(Address)``.

A lookup into a collection declared with ``DMibActorEntries`` (see
:doc:`malterlib-actor-phase <actor-phase>`), also into either of a conditional,
``(bChoose ? mp_First : mp_Second)[Key]``, when both lose entries in the same
phases of the same object, is kept across a suspension when
nothing else can remove its entry while the coroutine is suspended, while the
pointer holds the lookup, not where it may have been assigned something else
(assigning it after the use, ``pEntry = nullptr``, leaves the use kept, but
not changing it otherwise, through its address or in a lambda):

- the collection is node-stable, one of ``NodeStableContainers``, so that
  other calls that add entries leave this one where it is;
- with ``Owner``, the entry is the one this coroutine added by its fresh key,
  also when the key was made through a reference to what holds the
  collection, ``fg_RandomID(State.mp_Items)`` (not one a lambda got the key or the entry of, which the caller can remove
  while the lambda is suspended, nor one moved from, like
  ``mp_Items[fg_Move(Key)]``, which leaves the key empty, but not where it
  isn't evaluated, ``sizeof(mp_Items[fg_Move(Key)])``), so that the calls that remove their own
  entries don't remove it, unless this call removes it itself (not where it
  doesn't run, in a discarded branch or ``sizeof``), also through an
  alias of the collection, a conditional, a comma or a cast of it, ``(bChoose ?
  mp_Items : mp_Items).f_Remove(ID)``, but not of a copy of the object, ``auto
  Copy = mp_State; Copy.m_Items.f_Remove(ID)``, before a later use, also one a ``goto`` runs again
  (removing the entry of another
  fresh key leaves it; removing it in its ``fg_AsyncDestroy`` cleanup whose
  guard isn't awaited early, or in a ``g_OnScopeExit``, held in a local of a
  scope around the entry's variable and not used otherwise, like called
  early, which runs it when the scope ends, is fine); a
  loop over the collection is never kept, since it passes the entries of
  others, nor an iterator moved on from the entry, like ``++Iterator``;
- for each phase the collection loses entries in, a guard of the phase is
  alive at the use: a local variable, not ``static``, which registers only
  for the call that first runs it, that holds ``co_await
  f_CheckDestroyedOnResume()``, ``co_await
  fp_CheckStoppedOrDestroyedOnResume()``, another ``DMibActorPhaseGuard``
  (of the own actor, also when given it as an argument, but not another
  actor through ``mp_pOther.f_Get()`` or a handle to one, ``fs_Guard(Other)``
  of ``TCActor<CApp> Other``; casting the holder to ``void`` keeps
  it, and so does giving it to what takes a reference to const,
  ``fg_Inspect(Guard)``, and naming it where it isn't evaluated, ``sizeof(Guard)``, or in
  a branch of an ``if constexpr`` that is discarded; also through a local awaiter, ``co_await fg_Move(Awaiter)``,
  also forwarded, ``fg_Forward<decltype(Awaiter)>(Awaiter)``, as what it was
  last assigned, ``Pending = fg_Later()``,
  also a reference to one, or in both values of a conditional, also of what is awaited, ``co_await (bChoose ? A :
  B)``), or
  an ``fg_OnResume`` callback that returns an error (made by a function that
  can be looked into whose returns all are errors, or else one named
  for an error or exception, but not one that gets the current one, like
  ``fg_CurrentException()``, or one that gets what may be empty, like
  ``f_GetException()``, a message, or constructed from what describes
  it, like ``DMibErrorInstance(...)``), also in both branches of an ``if``, or
  throws, when the flag of the phase (also through a local reference to
  it that it captures by reference, ``bool const &bDestroyed =
  mp_bDestroyed``, not a copy)
  is set (also compared, like ``== true``, or read with ``load()``, like of
  an atomic flag), like ``if
  (mp_State.m_bStoppingApp || f_IsDestroyed())
  return DMibErrorInstance("Shutting down");``, before it can return
  success, and without a way to return success in that branch, nor starting
  the phase itself, also in a lambda it makes or a function it calls, like a
  ``f_StopWrapper()`` that calls ``f_StopApp()``. Awaiting a guard registers
  it without suspending, or ends the coroutine. Registering one runs only its
  own callback: a phase started since the others alive last ran theirs, on
  the last resume or when they were registered, like by a call of
  ``f_StopApp()``, is checked only by a guard of it registered after. The guard and
  the flags must be of the own actor, like ``this`` or ``pThis``, also the
  actor a static or free getter of a flag is given, like
  ``fs_IsStopping(pThis)``: a guard of another actor doesn't tell when this
  one is destroyed, and neither does a copy of the state that a callback
  captured, like ``[State]`` of ``auto &State = mp_State``. A callback
  registered after a guard, which runs after it on resume, can start its
  phase without the guard seeing it, like ``if (m_bStopNow) f_StopApp();``,
  also through a pointer to a member function, ``(this->*fStop)()``, which
  can start any phase unless it is an unchanged local made from one, and a
  virtual function, ``f_Tick()``, which can start what any override of it
  in a class the object can be starts, also one beside it that shares the
  class declaring the function as a virtual base, ``struct CSide : virtual
  CBase``, also one in a base of that class that a using declaration names
  the function past, ``using CBase::f_Tick;`` with ``CMid::f_Tick`` (none of
  another class, nor more when the class is ``final``), also of a local class of an instantiation of a
  function template, ``struct CDerived : t_CBase`` in ``fg_Make<CApp>()``, also in what they call however many, so the
  guard doesn't cover that phase while it lives; starting a phase of another
  actor, ``_pOther->f_StopApp()``, doesn't, and neither does a call in a
  branch an ``if constexpr`` discards, also in a function it calls. What it
  calls through an unchanged local pointer to a function, ``fStop(this)`` with
  ``auto fStop = &fg_Stop``, runs that function, and one through another
  pointer given the object can start any phase. What it
  constructs runs its constructor, its member initializers and later its
  destructor, which can start a phase too, ``CStop Stop(this)``, also of an
  aggregate, ``CStop Stop{this}``, and the destructors of its members and
  bases, which run after it, and every
  function it calls is looked into, however many. A ``return`` in a discarded
  branch doesn't return. A pointer copied from one that was
  pointed at a local, also moved, offset, cast or chosen by a conditional, ``pValue
  + 1``, points into the local.

When only the guard is missing, the warning names the phases it needs.
Calling a phase entry or function of the own actor, like ``co_await
f_StopApp()``, also through an unchanged local pointer to it or a copy of one,
or to a function given the object, ``fStop(this)`` with ``auto fStop =
&fg_Stop``, or any such function given the object when it can't be told,
in a lambda called there, or in a member function of the own actor that is
called there, like a ``f_StopWrapper()`` that calls it, or in another
function given the object, ``fg_Stop(this)`` (not one given another actor's
object, ``fs_Stop(_pOther)``, also an annotated one given a handle to another
actor, ``fs_Stop(Other)`` of ``TCActor<CApp> Other``, also one annotated as
an entry of the phase, whose parameter is that object in it, which
is followed again when it is given the own object), also a
constructor, ``CStop Stop(this)``, runs the phase there, and so does a scope
exit, ``g_OnScopeExit / [this] { f_StopApp(); }``, or a destructor that calls
it, also of a member or a base, ``struct CWrapper { CStop m_Stop; };``, where its scope ends (or where a discarded temporary scope exit is made,
and the destructor of a temporary at the end of its full-expression,
``(void)CStop{this}``) (not what a lambda doesn't run, like in a branch of an
``if constexpr`` that is discarded or ``noexcept(f_StopApp())``, also in a
callback of ``fg_OnResume``),
where no guard keeps the entries it removes.

A template is checked as each of its instantiations makes it, and one that
isn't instantiated isn't checked. A function whose control flow is too
complex to build, like through a long chain of default arguments that are
conditionals of calls that destroy temporaries, is reported, since what it
holds across its suspensions can't be checked.

Options
-------

.. option:: FreshKeyFunctions

   Semicolon-separated list of functions that make a key that no other entry
   of a collection has. None are built in; the Malterlib options file (:doc:`/clang-tidy/MalterlibOptionsFile`) adds `fg_RandomID`.

.. option:: NodeStableContainers

   Semicolon-separated list of containers whose entries stay where they are
   when others are added or removed. Built in are those of the standard library, `std::map;std::set;std::multimap;std::multiset;std::list;std::forward_list`, and the Malterlib options file (:doc:`/clang-tidy/MalterlibOptionsFile`) adds Malterlib's, like `TCMap`.
