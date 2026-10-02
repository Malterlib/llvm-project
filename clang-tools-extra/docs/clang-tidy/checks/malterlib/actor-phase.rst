.. title:: clang-tidy - malterlib-actor-phase

malterlib-actor-phase
=====================

Verifies the phase contracts that the annotations of
``Mib/Concurrency/TidyAnnotations`` declare. A phase is a part of the life of
an actor that, once it begins, lasts until the actor is gone:

- **Destroy**: the actor is destroyed. ``fp_DestroyInternal`` sets the flag
  that ``f_IsDestroyed()`` returns, then runs ``fp_Destroy``.
- **StopApp**: the app stops. ``f_StopApp`` sets ``m_bStoppingApp``, then runs
  ``fp_StopApp``.

The flags are only ever set, not cleared, also not by an output of assembly,
``asm("..." : "=m"(mp_bStopped))``, so what runs in a phase, and what it starts, like
a lambda or a call of the actor itself, runs with the flag set. That lets
``malterlib-coroutine-actor-reference`` accept data that is only removed in a
phase, when a guard of the phase, like ``fp_CheckStoppedOrDestroyedOnResume``,
ends the coroutine on resume once the phase began.

The annotations:

.. code-block:: c++

  // The flags, and the guards that end a coroutine on resume once they are set.
  DMibActorPhaseFlag(Destroy) bool f_IsDestroyed() const;
  DMibActorPhaseFlag(StopApp) bool m_bStoppingApp = false;
  DMibActorPhaseGuard(Destroy) TCFuture<COnResume> f_CheckDestroyedOnResume() const;
  DMibActorPhaseGuard(Destroy, StopApp) TCFuture<COnResume> fp_CheckStoppedOrDestroyedOnResume() const;

  // What sets the flag and then runs the phase, which is trusted.
  DMibActorPhaseEntry(Destroy) TCFuture<void> fp_DestroyInternal();

  // Functions that run only in one of the phases. An override has the phase
  // of the function it overrides.
  DMibActorPhase(Destroy) virtual TCFuture<void> fp_Destroy();
  DMibActorPhase(Destroy, StopApp) TCFuture<void> fp_DestroyAll();

  // A collection whose entries are removed only in the phases, and, with
  // Owner, by the call that added them.
  DMibActorEntries(Destroy, StopApp, Owner) TCMap<CStr, CDownload> mp_Downloads;

Each translation unit verifies what it uses, and the declarations are in the
headers that each one sees, so together they verify the whole program:

- An override runs in its own phases and in those of each function it
  overrides. A pointer to a ``DMibActorPhase`` member function must be called
  right away on the own object, like ``(this->*&CApp::fp_DestroyAll)()``.
- A ``DMibActorPhase`` function is only used in a function whose phases are
  all among its own, a ``DMibActorPhaseEntry`` of them, or a lambda made in
  one, and only on the object of the actor whose phase it is: ``this``, what
  is named for it, like ``pThis`` or ``m_pThis``, a parameter of a static or
  free function given the object of the actor it runs on, ``CApp *_pApp``
  (see :doc:`malterlib-actor-dispatch-this <actor-dispatch-this>`), also cast
  to another class of it, and either value of a conditional when both are,
  ``(bChoose ? this : pOwn)``, and what that holds by value or owns. A static or free function of
  phases that is given an actor, like ``fs_Clear(pThis)``, must be given the
  own object, also for what is part of an actor, like ``fg_Clear(&m_State)``
  and not ``fg_Clear(&pOther->m_State)``, also through a local pointer to it,
  ``auto *pState = &pOther->m_State``, and called right away: a pointer to
  it can be called by another actor. An override with phases, also one in
  an instantiation of a template, ``TCApp<CBase>::f_Clear``, also
  annotated on its definition or inherited from one of the functions it
  overrides, runs in those of all of them, so each must be annotated too:
  callers of one that isn't may call it in any phase.
- A ``DMibActorPhaseFlag`` member, also a static one, through its definition
  outside of its class too, or a structured binding of it, is only set to
  ``true`` (and what that assignment gives is the flag too, and so is what a
  function gives back of it, ``fg_Const(m_bDestroyed)``), and no pointer to
  it as a non-const member, like ``&CApp::m_bDestroyed``, is taken: by
  assignment of what is true, also a constant, ``bFlag = c_bYes``, by or-ing,
  ``bFlag |= bOther``, also in the value, ``bFlag = bFlag || bOther`` or
  ``bFlag = bOther || true`` (not when what is or-ed after the flag suspends,
  ``bFlag = bFlag || co_await fg_Work()``, which assigns what the flag was
  before it was set while suspended, nor when it starts the phase, ``bFlag =
  bFlag || f_Begin()``, also by setting the flag in a function it calls,
  which sets the flag before it is assigned what it was), or
  for an atomic, ``store(true)``, ``exchange(true)`` or
  ``compare_exchange_strong(Expected, true)``, and for Malterlib's,
  ``f_Store(true)``, ``f_Exchange(true)`` or
  ``f_CompareExchangeStrong(Expected, true)``, also of a constant that is
  true, ``store(c_bYes)``. Its other non-const
  member functions and operators, like ``store(false)`` or ``++``, and binding
  it to a non-const reference or pointer, can clear it.
- A ``DMibActorEntries`` collection only loses entries in a function of its
  phases, on its own object, or in the constructor or destructor of its
  class (not in a lambda made there that isn't called there, which can run
  later, like a callback kept in a member, nor a coroutine lambda, which goes
  on after it suspends, but one held in a local that is only called there
  is), or of a class derived from it, of the object being constructed or
  destroyed, also in what it holds by value, ``mp_State.m_Items`` (a default member initializer of another class, or the
  initializer of a variable, runs outside of any phase). What loses entries is a member function of ``RemovingMethods``, also
  through an iterator of ``IteratorMethods`` (also of a free ``begin``, and
  copies of the iterator, also assigned or moved ones, or chosen by a
  conditional; returning one, keeping it in another object, calling a member
  function of it through a pointer to it, or giving it to a function other
  than one of the standard library that doesn't call what it is given, like
  ``std::invoke``, lets that remove anywhere), taking a pointer to it as a non-const
  member,
  assigning the collection, also by name with ``operator=``, a member operator
  of it that can change it, ``mp_Items -= Other`` (not a const one, nor one
  whose explicit object is a reference to const or a copy, ``operator&(this
  CSet const &_Self, CSet const &_Other)``, nor a lookup or one that adds),
  calling a member
  function of it through a pointer to it, passing it on by
  a non-const reference, like to
  ``fg_Move``, or a non-const pointer or reference to it that does so (also
  what a function given it by a const reference gives back of it,
  ``const_cast<TCMap<int, int> &>(fg_Const(mp_Items)).f_Clear()``), also a
  const one that is cast to a non-const one, ``const_cast<TCMap<int, int>
  &>(Items)``, also by a C-style cast, ``(TCMap<int, int> &)Items``, also of
  what a pointer to const points to, ``const_cast<TCMap<int, int>
  &>(*pItems)``, also one a getter gives, ``*f_Items()``, also kept in a local,
  ``auto pItems = f_Items()``, like a flag, ``const_cast<bool &>(bStopping) = false``, also
  through a local pointer to const, ``*const_cast<bool *>(pStopping) =
  false`` or ``const_cast<bool &>(pStopping[0]) = false``, also a reference bound to what it points to, ``bool const &bAlias
  = *pStopping``, and copies of it, ``bool const *pCopy = pStopping``, also a
  reference to it, ``bool const *&pAlias = pStopping``, also as a
  value of a conditional or the last of a comma, ``bChoose ? pStopping :
  &bLocal``, where it still points there (one cast to a pointer that isn't const and kept is
  reported where it is cast), and of a collection,
  ``const_cast<TCMap<int, int> *>(pItems)->f_Clear()``, also through another
  const alias made of one, ``auto const &Second = First``, but not where it
  doesn't run, ``sizeof(...)`` (not
  its address given as a pointer to const, ``fg_Read(&mp_Items)`` of
  ``fg_Read(TCMap<int, int> const *_pItems)``), which
  keeps the object of what it was made from, also through a structured
  binding, a pointer it is assigned to, or a copy of a pointer to it that a
  lambda captured (a pointer, until a statement before the use points it
  elsewhere on every path). Templates are checked in their
  instantiations. With ``Owner``, a function may
  also remove an entry by a key that it made with a function of
  ``FreshKeyFunctions`` from the same collection of the same object, like
  ``fg_RandomID(mp_Downloads)``, and added the entry with, also through
  unchanged copies of the key and in a lambda it made, like a cleanup.
  Assigning an object that holds annotated collections or a phase flag
  replaces each of them, and is checked the same way, and so is moving it
  out, like ``CState Old = fg_Move(mp_State)``, also either of a conditional,
  ``fg_Move(bChoose ? mp_First : mp_Second)``, or through any number of local
  references to it, also through an owning
  pointer, ``fg_Move(*mp_pState)``, or a local pointer to it,
  ``fg_Move(*pState)`` of ``auto *pState = &mp_State``, which takes the phase flags
  it holds, also through an owning pointer; a local copy of it, ``auto Copy =
  mp_State``, has collections of its own; so is replacing or moving out
  an owning pointer, like ``mp_pState = nullptr`` of a
  ``TCSharedPointer<CState>`` (one with an ``operator->``, also inherited or
  instantiated from a template, whose destructor can release something, not an
  observer or an iterator, nor one whose destructor only assigns or does
  nothing, ``~CObserver() {}``), which destroys the collections and the phase
  flags of what it owns, but not a handle to an actor, and so is swapping, like
  ``std::swap(mp_State, Other)``, also ``std::ranges::swap``, also of an array
  of them,
  ``std::swap(mp_States, Empty)``, destroying it, ``mp_State.~CState()``, also
  ``std::destroy_at(&mp_State)``, making it again where it is, ``new
  (&mp_State) CState{}`` or ``std::construct_at(&mp_State)``, also
  a collection itself, ``mp_Items.~TCMap()``, and exchanging, like
  ``std::exchange(mp_State, CState{})``. A function of the standard library that calls
  what it is given, like ``std::async``, or an algorithm given a function
  object, can use an iterator it is given like that function. Pointing a
  pointer to it elsewhere, also through a reference to the pointer or a
  pointer to it, ``*ppItems = nullptr``, doesn't change it. A local pointer pointed elsewhere on
  every path is of something else, but not when it is pointed at what may
  still be the collection, like ``pItems = bChoose ? pItems : &Local``, also
through a copy of it kept before, nor
  when a jump can pass the assignment. An iterator made from an
  iterator, like ``iEntry.f_GetIterator()``, is of the same collection, and
  so is what an exchange of it returns, ``std::exchange(iEntry, {})``, one
  given to another object, ``CHolder Holder(iEntry)``, is kept there, where it
  can remove the entry, and a binding of a copy, ``auto [Items] = mp_State``,
  is of the copy. Clearing a pointer that owns an object holding annotated
  collections, like ``mp_pState.f_Clear()`` or ``reset()``, or detaching
  what it owns, ``f_Detach()`` or ``release()``, removes their
  entries, like assigning it.

.. code-block:: c++

  TCFuture<void> f_Download()
  {
    auto DownloadID = fg_RandomID(mp_Downloads);
    auto &Download = mp_Downloads[DownloadID];
    auto Cleanup = g_OnScopeExit / [&]
      {
        mp_Downloads.f_Remove(DownloadID); // OK, the entry of this call
      }
    ;
    mp_Downloads.f_Clear(); // warning: not in Destroy or StopApp
    co_await fp_DestroyAll(); // warning: not in Destroy or StopApp
  }

A local pointer initialized or assigned the address of a collection, also
by ``std::addressof``, also through a cast or as either value of a
conditional, ``bMember ? pItems : &Local``, is followed (not the value a
constant condition doesn't select, ``(false ? mp_Items : Local).f_Clear()``),
and what a branch of an ``if constexpr`` that is discarded does
never runs, and so does what a constant condition never takes, ``if (false)``
or ``while (false)``, nor what isn't evaluated, like ``noexcept(Items.f_Clear())``,
also through a reference or iterator, and a pointer assigned the collection
on some paths, ``if (bActor) pItems = &mp_Items;``, is of it. Copies of an
iterator are followed however many there are. An iterator assigned another one on
every path is of what that one is of, also where it is returned, but one
assigned a conditional that can keep it, ``iEntry = bKeep ? iEntry :
iOther``, is still of the collection. A pointer to a collection held inside another object, like an element of an
array, ``decltype(pItems) Aliases[] = {pItems}``, is not followed.

Options
-------

.. option:: RemovingMethods

   Semicolon-separated list of member functions that remove entries. A
   trailing `*` matches any end. Built in are those of the standard library, `erase;clear;pop_back;pop_front;swap;extract;resize;erase_after;remove;remove_if;unique;assign;splice;splice_after`, and the Malterlib options file (:doc:`/clang-tidy/MalterlibOptionsFile`) adds Malterlib's, like `f_Remove*` and `f_Clear*`.

.. option:: IteratorMethods

   Semicolon-separated list of member functions that give an iterator, whose
   removing member functions remove entries. Built in are `begin;end;rbegin;rend`, and the Malterlib options file (:doc:`/clang-tidy/MalterlibOptionsFile`) adds `f_GetIterator*`.

.. option:: FreshKeyFunctions

   Semicolon-separated list of functions that make a key that no other entry
   of a collection has. None are built in; the Malterlib options file (:doc:`/clang-tidy/MalterlibOptionsFile`) adds `fg_RandomID`.
