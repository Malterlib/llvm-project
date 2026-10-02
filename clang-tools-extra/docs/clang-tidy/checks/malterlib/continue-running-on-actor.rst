.. title:: clang-tidy - malterlib-continue-running-on-actor

malterlib-continue-running-on-actor
===================================

Finds uses of the object of an actor in a coroutine after
``co_await fg_ContinueRunningOnActor(Other)`` moved the coroutine to another
actor, on any path until it switches back to the own actor. The object belongs
to its own actor, so using it from another one races with it.

.. example-before
   struct CInternal { int m_Value = 0; };
   struct CExampleActor : CActor
   {
     TCFuture<int> f_Example()
     {

.. example-after
       co_return 0;
     }
     CInternal *mp_pInternal = nullptr;
   };

.. code-block:: c++

  auto &Internal = *mp_pInternal;
  auto ThisActor = fg_ThisActor(this);
  auto BlockingActorCheckout = fg_BlockingActor();
  co_await fg_ContinueRunningOnActor(BlockingActorCheckout);
  Internal.m_Value = 1;  // warning
  co_await fg_ContinueRunningOnActor(ThisActor);
  Internal.m_Value = 2;  // fine

Nothing keeps the actor alive while the coroutine runs on another one, so its
object can be gone by then. Reading ``self`` or a handle member, or making a
handle with ``fg_ThisActor(this)``, after switching away is a use of the object
too; make a handle to switch back with before switching away, like
``ThisActor`` above, also given moved, ``fg_Move(ThisActor)``, or as either of
a conditional of such handles, ``bChoose ? ThisActor : ThisActorCopy``. Static members can be used from any actor.

The calls of the promise that ``co_return`` and ``co_yield`` make are plumbing,
never looked into: the conversion a ``co_return`` performs into the result is
judged by type, and what its operand uses is reported once, where it is used,
``co_return m_Value.f_Read((co_await fg_ContinueRunningOnActor(Other), 1));``
at ``m_Value``.

The switch can also be made before and awaited later, like ``auto Switch =
fg_ContinueRunningOnActor(Other); co_await fg_Move(Switch);``, also made from
a conditional of switches, also through a
reference to it, also assigned through a cast to a reference, or as either of
a conditional, ``(bChoose ? Back : Spare) = ...``, also through a reference
bound to one,
``static_cast<CCoroutineTransferOwnership &>(Back) = ...``, as a pointer can be
pointed into the object, ``static_cast<int *&>(pValue) = &m_Value``, also
through a reference bound to either of a conditional, ``pAlias = &m_Value``
of ``int *&pAlias = bChoose ? pFirst : pSecond``, which may be either, also as
either of a conditional, ``(bFirst ? pFirst : pSecond) = &m_Value``, but not where it
doesn't run, like in a branch of ``if (false)`` or after a ``return`` that no
label follows. A handle assigned another, also through a cast to a
reference, ``static_cast<decltype(ThisActor) &>(ThisActor) = Other``, or
through a reference or pointer to const whose const is cast away,
``const_cast<decltype(ThisActor) &>(Alias) = Other``, isn't
the own actor anymore, but one assigned only where it doesn't run, in
``sizeof`` or a discarded branch, still is. A stored switch that is assigned another may switch anywhere,
so it is taken as a switch to another actor, and so is a switch that can't be
told, like through a pointer to ``fg_ContinueRunningOnActor``, a transfer given
as a parameter, also of a class derived from ``CCoroutineTransferOwnership``
or its awaiter, or a wrapper whose ``operator co_await`` gives that awaiter,
``fg_ContinueRunningOnActor(Target).operator co_await()``, or one changed by ``fg_Exchange`` or what gets it by a
non-const reference, also through a virtual function an override of which can
assign it, or that a pointer to it that isn't to const can assign, ``*pBack =
...``. Awaiting it, also moved, ``co_await fg_Move(Back)``, also moved into
another local, ``auto Moved = fg_Move(Back)``, doesn't change where it
switches. Of two switches
in a conditional, one to another actor is a switch to another actor. A member
function called after a switch in its arguments, like ``f_Update((co_await
fg_ContinueRunningOnActor(Other), 1))``, runs after the switch, also through
``(*this).f_Update(...)``, also on a lookup of the object, ``mp_Items[0].f_Read(...)``, and what such a call is
given by reference or pointer is used after the switch too, also as either
value of a conditional, ``bChoose ? &m_Value : &g_Value``, and so is what a
pointer into the object points to accessed after a switch in the same
expression, ``pValue[(co_await fg_ContinueRunningOnActor(Other), 0)]``, also
when the expression points it elsewhere after it is read, ``pValue[(co_await
fg_ContinueRunningOnActor(Other), pValue = &Local, 0)]``, which is taken to
point into the object when it can. A pointer copied from a pointer member of
the object points where the class points the member, ``mp_pConfig =
&gc_Configuration``, only when the member states that only its class points
it (``DMibSetByOwnClass``); else what it points to can't be told and is
taken to be the object.

Uses are accesses of members of ``this``, calls of member functions, and uses
of pointers and references initialized from ``this``, like ``Internal`` above
or a captured ``pThis``, also the names of a structured binding of a
reference into it, ``auto &[Value] = m_Data``, also through the first value
of ``&m_Value ?: &g_Value``, which is its condition, also a static local or a parameter pointed into it,
also pointed into it by assignment, a swap,
``fg_Swap(pValue, pOther)``, also through a cast to a reference, ``fg_Exchange`` or
``std::addressof`` (but not the address of a static member, like
``&this->mc_Value``, nor a static member named through a conditional that
chooses it, ``(bChoose ? this : pOther)->mc_Value``), also the entries of a range-based ``for`` loop over its
data, ``for (auto &Value : m_Values)``, and iterators a member function of it
returns, also kept in a local, ``auto iEntry = m_Values.f_GetIterator()``, and of parameters that point or refer to the object of
an actor or its internals, like the ``CInternal *_pThis`` of a static function.
A function without an actor of its own also uses the objects of actors that its
parameters, or a lambda's captures, point or refer to. A function without
``this`` and with one such parameter switches back with
``fg_ThisActor(_pThis)`` or ``_pThis->self``, which use the object when made
after switching away, or through a handle made before, by what it holds where
the switch back awaits it (one changed only after still switches back); switching to the actor
of a parameter doesn't make the object of ``this``, or of another parameter,
safe to use. The analysis follows the control flow of the coroutine, so a use
counts when any path to it has switched away. A function with an actor of its
own doesn't track which actor it is on for the objects of other actors it gets
in parameters or captures, so their use after any switch is reported, also
after switching back to its own actor. A pointer or reference into such an
object that a lambda captures, like ``int *pValue = &_pActor->m_Value`` or
``[pValue = &_pActor->m_Value]``, is followed to where the lambda is made, also
through the captures of lambdas around it, and anywhere it is given a value
for a capture by reference, which sees later assignments; a copy of what a
reference refers to, ``[Value]`` of ``int &Value``, is a value; with such a capture, switching to the
actor of a parameter doesn't switch back. The only actor pointer a lambda
captures is switched back to like a parameter when it isn't changed: by the
body of a mutable lambda, or anywhere for a capture by reference. A template is
checked as each of its instantiations makes it, like ``t_CActor *_pActor`` of
an actor, and one that isn't instantiated isn't checked. A lambda in a default member initializer of an actor has the actor
of the object it is made in.

A lambda that runs where it is made, like through ``fg_CallSafe`` or
``std::invoke``, also in another lambda that does, runs on the
actor the coroutine is on until it switches by itself: what it uses before its
first switch that it awaits unconditionally, not in a branch or a loop, nor
in a lambda with a jump, which can pass it, is
checked like a use of the coroutine, but not what in it can't run: after a
return, in a branch a constant condition never takes or in a loop that never
runs, ``while (false)``, or in a lambda made where code can't run; the
``default`` of a ``switch`` over each enumerator of an enum, and what follows
one without a ``default``, run, since an enum can hold a value no enumerator
names. So is a lambda dispatched to the current
actor, like ``g_Dispatch / [this] { ... }``, ``fg_Dispatch(Lambda)`` or a timer
callback without an actor, which runs on the actor the coroutine is on there,
and a scope exit, like ``auto Cleanup = g_OnScopeExit / [this] { ... }``, also
made in both values of a conditional of the same lambda, ``bChoose ?
g_OnScopeExit / fg_Move(fCallback) : g_OnScopeExit / fg_Move(fCallback)``, at
the end of the scope of its holder, or where it is made when it is a
discarded temporary, ``(void)(g_OnScopeExit / [this] { ... })``, also one it is moved to, ``auto Moved =
fg_Move(Cleanup)``, also through a local reference to it, ``fg_Move(Alias)``, where it runs, and not where the one moved from ends,
unless cancelled by ``f_Clear()`` or run early, ``Cleanup()``, on every path before, and so does
the cleanup of ``fg_AsyncDestroy``, also of guards held in an array, ``CGuard
Guards[] = {co_await fg_AsyncDestroy(...)}``, unless its guard was moved away on every
path before, and also where it is awaited early, like ``co_await
fg_Move(Cleanup).f_Wrap()``. A scope exit that dispatches to the actor it is
made on, ``g_OnScopeExitActor / [this] { ... }``, is checked where it is
made. A lambda that a lambda checked like this calls, like ``fRead()`` in
``[&fRead] { return fRead(); }``, is checked with it, also through any number
of lambdas that call each other, but assigning what it captures,
``pValue = nullptr``, or comparing or testing it where the steps that hand its
value on unchanged end, also in the body of a function it is given to that is
seen, ``pValue == nullptr`` or ``(bChoose ? pValue : nullptr) == nullptr``,
doesn't use what it points to, and what it reads
through its copy after pointing it elsewhere, ``[pValue, &Local]() mutable {
pValue = &Local; return *pValue; }``, is that. What a lambda that is
called assigns to a pointer it captures by reference, ``[&pValue] { pValue =
&m_Value; }``, also one that ``fg_CallSafe`` or ``std::invoke`` runs, is where the pointer points,
and what a lambda called where it is made returns, also of what it is given,
``[](int &_Value) { return &_Value; }(m_Value)``, also through ``std::invoke``, points there too
after the call (for sure only before what can leave it, also a call that can
throw where what it throws is caught, ``try { fRedirect(); } catch (...)
{}``). The callback of ``fg_OnResume`` is checked at
each switch to another actor while it is registered, since it runs where the
coroutine resumes, also by a temporary registration at a switch later in its
full expression, ``((void)co_await fg_OnResume(...), co_await
fg_ContinueRunningOnActor(Other))``, also either of a conditional, ``bChoose ? co_await
fg_OnResume(A) : co_await fg_OnResume(B)`` or ``co_await (bChoose ?
fg_OnResume(A) : fg_OnResume(B))``, and so is a phase guard of the
actor, like ``co_await f_CheckDestroyedOnResume()``, whose callback reads the
object, also awaited through an awaiter made before, ``co_await
fg_Move(Awaiter)``, or a reference to it. Clearing the guard of ``fg_AsyncDestroy``, ``Cleanup.f_Clear()``,
cancels its cleanup, and so does moving it away, ``fg_Move(Cleanup)`` or given
to an rvalue reference, but not giving it to a reference that isn't to an
rvalue or taking its address, which may keep it; a guard of either value of a
conditional is followed too.

A call through a pointer to a member function, ``(this->*fRead)(...)``,
whose arguments switch, uses the object after the switch, and so do a call
through a pointer to a function and a constructor that are given it by
reference or pointer, ``CCopy{m_Value, (co_await ...)}``. What an exchange of
a local pointer returns, the value it had,
``fg_Exchange(pValue, nullptr)``, points where it pointed, and what an
assignment gives, ``pSecond = (pFirst = &m_Value)``, where what it assigns
does. A pointer or reference into the object held inside a local, a
borrowed member of a struct or a view, ``*Copy.m_pValue`` of ``CMixedRef
Copy{1, &m_Value}``, also of a copy of one, and what an accessor of one gives,
``Copy.f_Items()``, or an operator of a copy of a shared owner,
``pCopy->m_Count``, is the object, as what the source's leads to is, but not
the copy's own storage, ``Copy.m_Value``. What a raw pointer the object holds
points to isn't the object, also read through an accessor, of the object, a
member of it or a copy of one, ``View.f_GetConfiguration()`` returning
``*m_pConfiguration`` of ``auto View = m_View``. A getter of a local view
gives what the member it returns holds, ``Buffers.f_Output()`` returning
``m_pOutput`` of ``CBuffers Buffers{&m_Value, &Value}``, also of a copy of
one, made or assigned by what the compiler defines, ``auto Copy = Buffers;``
or ``Copy = Buffers;``, which holds what each member of it held, and a
pointer read from a member of a local view points where that member does,
``auto *pOutput = Buffers.m_pOutput;``, not what all of the view is made of, and a constant condition, also a named one, chooses what a
view is made of. A handler of a ``try`` gets what that ``try`` leaves, not
what one around it or in it does. An integer made of the value of a
pointer, ``auto Address = reinterpret_cast<umint>(&m_Value)``, also through
offsets and other integer locals made of it, is that value until it is cast
back: ``*reinterpret_cast<int *>(Address) = 1`` after the switch is a use of
the object, also in a lambda that copies it, while comparing, logging or
giving it as a number isn't. A
generic lambda is checked as each body its call operator is instantiated with,
never as written. A function whose control flow is too complex to build, like through a long
chain of default arguments that are conditionals of calls that destroy
temporaries, is reported, since what it uses after it switches can't be
checked.

Nothing is reported in a translation unit with an error that stops it from
compiling: its control flow isn't built, and what the errors leave can't be
relied on.

.. example-check

  struct CApp : CActor
  {
    int m_Value = 0;
    static int ms_Value;
    TCActor<CActor> m_Other;
    void f_Update(int _Value);
    TCFuture<void> f_Switched()
    {
      auto ThisActor = fg_ThisActor(this);
      co_await fg_ContinueRunningOnActor(m_Other); // warning: a member used after switching away
      m_Value = 1;
      co_await fg_ContinueRunningOnActor(ThisActor); // fine: after switching back
      m_Value = 2;
      co_return {};
    }
    TCFuture<void> f_Static(TCActor<CActor> _Other)
    {
      co_await fg_ContinueRunningOnActor(_Other); // fine: a static member
      ms_Value = 1;
      co_return {};
    }
    TCFuture<void> f_LateHandle(TCActor<CActor> _Other)
    {
      co_await fg_ContinueRunningOnActor(_Other); // warning: a handle made after switching away
      co_await fg_ContinueRunningOnActor(fg_ThisActor(this));
      co_return {};
    }
    TCFuture<void> f_NotRun(TCActor<CActor> _Other)
    {
      if (false) // fine: a switch that doesn't run
        co_await fg_ContinueRunningOnActor(_Other);
      m_Value = 1;
      co_return {};
    }
    TCFuture<void> f_InArguments(TCActor<CActor> _Other)
    {
      f_Update((co_await fg_ContinueRunningOnActor(_Other), 1)); // warning: a call after the switch in its arguments
      co_return {};
    }
    TCFuture<void> f_Pointer(TCActor<CActor> _Other)
    {
      int *pValue = &m_Value; // warning: a pointer into the object
      co_await fg_ContinueRunningOnActor(_Other);
      *pValue = 1;
      co_return {};
    }
    TCFuture<void> f_PointedElsewhere(TCActor<CActor> _Other)
    {
      int Local = 0; // fine: pointed elsewhere before it is read
      int *pValue = &m_Value;
      co_await fg_ContinueRunningOnActor(_Other);
      pValue = &Local;
      *pValue = 1;
      co_return {};
    }
    TCFuture<void> f_ScopeExit(TCActor<CActor> _Other)
    {
      auto Cleanup = g_OnScopeExit / [this] // warning: a scope exit runs at the end of its scope
        {
          m_Value = 0;
        }
      ;
      co_await fg_ContinueRunningOnActor(_Other);
      co_return {};
    }
    TCFuture<void> f_ScopeExitCleared(TCActor<CActor> _Other)
    {
      auto Cleanup = g_OnScopeExit / [this] // fine: cancelled before the switch
        {
          m_Value = 0;
        }
      ;
      Cleanup.f_Clear();
      co_await fg_ContinueRunningOnActor(_Other);
      co_return {};
    }
    TCFuture<void> f_Integer(TCActor<CActor> _Other)
    {
      auto Address = reinterpret_cast<unsigned long>(&m_Value); // warning: a pointer converted to an integer and back
      co_await fg_ContinueRunningOnActor(_Other);
      *reinterpret_cast<int *>(Address) = 1;
      co_return {};
    }
    TCFuture<void> f_IntegerCompared(TCActor<CActor> _Other)
    {
      auto Address = reinterpret_cast<unsigned long>(&m_Value); // fine: only compared as a number
      co_await fg_ContinueRunningOnActor(_Other);
      (void)(Address == 0);
      co_return {};
    }
  };

Options
-------

.. option:: AllowedMemberTypes

   Semicolon-separated list of the types of members that are safe to read from
   any actor, for objects that are kept alive while the coroutine runs on
   another actor. Default is empty.
