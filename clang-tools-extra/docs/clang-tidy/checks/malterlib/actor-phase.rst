.. title:: clang-tidy - malterlib-actor-phase

malterlib-actor-phase
=====================

Verifies the phase contracts that the annotations of
``Mib/Core/TidyAnnotations`` declare. A phase is a part of the life of
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
ends the coroutine on resume once the phase began; one called on the object
or given it, ``fs_CheckDestroyedOnResume(pThis)``, only when that can't be
null: ``fs_CheckDestroyedOnResume(bKeep ? this : nullptr)`` and
``fs_CheckDestroyedOnResume(dynamic_cast<CApp *>(pBase))`` don't guard, also
through casts, local aliases, ``*&pMaybe``, helpers that give back what they
are given, ``fg_Move(pMaybe)``, and accessors, ``f_MaybeSelf(bKeep)``, as
:doc:`malterlib-coroutine-actor-reference <coroutine-actor-reference>` tells
what an expression is relative to the object. Another argument that can be
null, like a message, ``f_Guard(nullptr)``, or a member of the actor,
``fs_Guard(this, bKeep ? &m_Context : nullptr)``, doesn't change what it
guards.

The annotations:

.. example-skip the declarations of the annotations, which say nothing of a report

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

- What an object made where it is runs, its constructor and destructor, and
  the member functions they call on it, ``~CStop() { f_Cleanup(); }``, can
  start the phases of the own actor, through what it is given or keeps,
  ``m_pThis->f_StopApp()``, not its own.
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
  ``(bChoose ? this : pOwn)``, the one a constant condition chooses, the right
  of a comma, and what that holds by value or owns: a member, also of a
  member, an element of an array member, or what an owning pointer member
  points to, unless that is another actor, not what a pointer or reference
  member points to, ``m_pOther``, also through a local alias of it, as
  :doc:`malterlib-coroutine-actor-reference <coroutine-actor-reference>`
  tells what an expression is relative to the object. A static or free function of
  phases that is given an actor, like ``fs_Clear(pThis)``, must be given the
  own object, also for what is part of an actor, like ``fg_Clear(&m_State)``
  and not ``fg_Clear(&pOther->m_State)``, also through a local pointer to it,
  ``auto *pState = &pOther->m_State``, each value it can be by itself, the
  values of a conditional, also through a cast, a comma or a local alias,
  ``CState *pState = bOwn ? &m_State : &Local``, where a local isn't
  another actor's, and called right away: a pointer to
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
  class (not in a lambda made there that can run after it returns, like a
  callback kept in a member, also one a default member initializer keeps, or a
  scope guard that is shared or whose value is moved away, nor a coroutine
  lambda, which goes on after it suspends; but one only called there, held in
  a local that is only called, given to what only calls it, ``fg_CallSafe``, or
  to a local scope guard, ``g_OnScopeExit / [this] { ... }`` or
  ``fg_OnScopeExit([this] { ... })``, is), or of a class derived from it, of the object being constructed or
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
  ``TCSharedPointer<CState>`` (an owner of ``OwnerTypes`` of the ``shared`` or
  ``unique`` kind; any other class holds only its by-value members, whatever
  its destructor does, so clearing a ``TCAutoClearPtr<CState>`` destroys
  nothing of the ``CState``), which destroys the collections and the phase
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
  is of the copy. Clearing an owner of ``OwnerTypes`` that owns an object
  holding annotated collections, like ``mp_pState.f_Clear()`` or ``reset()``,
  or detaching what it owns, ``f_Detach()`` or ``release()``, removes their
  entries, like assigning it; a member function of the same name of another
  class that isn't const and whose body can't be seen,
  ``mp_State.f_CreateNew()`` of a ``TCOptional<CState>``, can replace what
  that class holds by value, so it removes the entries and overwrites the
  phase flags that holds, like assigning it, and nothing of what it only
  points to; one whose body can be seen is told by what it does. A hand-written owner is one where it is listed in
  ``OwnerTypes``, ``CRound149Owner=unique``: what it owns, also through its
  ``operator->``, is then removed by its ``f_Reset()``; one that isn't listed
  owns nothing that can be told.

.. example-headers malterlib-actor-phase.h malterlib-concurrency-void.h

.. example-before
   struct CDownload
   {
   };
   struct CApp : CDistributedAppActor
   {
     DMibActorPhase(Destroy, StopApp) TCFuture<void> fp_DestroyAll();
     DMibActorEntries(Destroy, StopApp, Owner) TCMap<CStr, CDownload> mp_Downloads;

.. example-after
   };

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
or ``while (false)``, what comes after a return or a ``switch`` whose every case,
its ``default`` among them, returns, what a lambda made where code can't run does,
nor what isn't evaluated, like ``noexcept(Items.f_Clear())``; the ``default``
of a ``switch`` over each enumerator of an enum, and what follows one without
a ``default``, run, since an enum can hold a value no enumerator names,
also through a reference or iterator, and a pointer assigned the collection
on some paths, ``if (bActor) pItems = &mp_Items;``, is of it. Copies of an
iterator are followed however many there are. An iterator assigned another one on
every path is of what that one is of, also where it is returned, but one
assigned a conditional that can keep it, ``iEntry = bKeep ? iEntry :
iOther``, is still of the collection. A pointer to a collection put in an element of a local array,
``decltype(pItems) Aliases[] = {pItems}``, holds its value there: a read of
that element, ``Aliases[0]``, or of one whose index isn't a constant, is an
alias of the collection, and the array given or kept whole, like given to a
function, can change it, reported where the pointer is put in it; put in a
member of a record, or in a list of another kind, it is reported there when
what holds it is written, given or kept (by the shared forward walk), as the
address of the collection put there, ``{&mp_Items}``, is.

Nothing is reported in a translation unit with an error that stops it from
compiling: its control flow isn't built, and what the errors leave can't be
relied on.

.. example-headers malterlib-actor-phase.h malterlib-concurrency-void.h

.. example-check

  struct CDownload
  {
  };
  struct CItems
  {
    DMibActorEntries(Destroy) TCMap<CStr, CDownload> m_Items;
  };
  void fg_Read(TCMap<CStr, CDownload> const *_pItems);
  struct CApp : CDistributedAppActor
  {
    DMibActorPhase(Destroy, StopApp) TCFuture<void> fp_DestroyAll();
    DMibActorEntries(Destroy) TCMap<CStr, CDownload> mp_Items;
    DMibActorEntries(Destroy, Owner) TCMap<CStr, CDownload> mp_Owned;
    TCSharedPointer<CItems> mp_pState;
    CItems mp_ByValue;
    TCFunction<void ()> mp_fLater;

    void f_Remove() // warning: not in a function of Destroy
    {
      mp_Items.f_Clear();
    }
    DMibActorPhase(Destroy) TCFuture<void> fp_Clear() // fine: in Destroy
    {
      mp_Items.f_Clear();
      mp_ByValue.m_Items.f_Clear();
      co_return {};
    }
    CApp() // fine: the constructor of its class
    {
      mp_Items.f_Clear();
    }
    ~CApp() // fine: a lambda held in a local that is only called there
    {
      auto fClear = [this] { mp_Items.f_Clear(); };
      fClear();
    }
    void f_Later() // warning: a lambda that is kept can run later
    {
      mp_fLater = [this] { mp_Items.f_Clear(); };
    }
    void f_ClearFlag() // warning: a flag is only ever set to true
    {
      mp_State.m_bStoppingApp = false;
    }
    void f_SetFlag(bool _bOther) // fine: set, or or-ed
    {
      mp_State.m_bStoppingApp = true;
      mp_State.m_bStoppingApp |= _bOther;
      mp_State.m_bStoppingApp = mp_State.m_bStoppingApp || _bOther;
    }
    TCFuture<void> f_OrSuspended() // warning: or-ed with what suspends
    {
      mp_State.m_bStoppingApp = mp_State.m_bStoppingApp || co_await TCFuture<bool>();
      co_return {};
    }
    void f_Phase() // warning: a function of the phases used outside of them
    {
      (void)fp_DestroyAll();
    }
    void f_PhaseLater() // warning: a pointer to a member function of the phases
    {
      auto pMember = &CApp::fp_DestroyAll;
      (void)pMember;
    }
    void f_Read() // fine: given as a pointer to const
    {
      fg_Read(&mp_Items);
    }
    void f_NotRun() // fine: what doesn't run, or isn't evaluated
    {
      TCMap<CStr, CDownload> Local;
      (false ? mp_Items : Local).f_Clear();
      if (false)
        mp_Items.f_Clear();
      while (false)
        mp_Items.f_Clear();
      (void)noexcept(mp_Items.f_Clear());
    }
    void f_SomePaths(bool _bActor) // warning: a pointer assigned the collection on some paths
    {
      TCMap<CStr, CDownload> Local;
      TCMap<CStr, CDownload> *pItems = &Local;
      if (_bActor)
        pItems = &mp_Items;
      pItems->f_Clear();
    }
    void f_Elsewhere() // fine: a pointer pointed elsewhere on every path
    {
      TCMap<CStr, CDownload> Local;
      TCMap<CStr, CDownload> *pItems = &mp_Items;
      pItems = &Local;
      pItems->f_Clear();
    }
    void f_Replace() // warning: replacing an owner destroys what it owns
    {
      mp_pState = nullptr;
    }
    void f_Assign() // warning: assigning an object that holds the collection
    {
      mp_ByValue = CItems();
    }
    void f_Copy() // fine: a local copy has collections of its own
    {
      auto Copy = mp_ByValue;
      Copy.m_Items.f_Clear();
    }
    void f_Fresh() // fine: the entry this call added, by its fresh key
    {
      auto ID = fg_RandomID(mp_Owned);
      mp_Owned[ID];
      mp_Owned.f_Remove(ID);
    }
    void f_NotFresh(CStr const &_ID) // warning: a key the call didn't make
    {
      mp_Owned.f_Remove(_ID);
    }
    void f_InArray() // warning: an element of an array holds the pointer
    {
      auto *pItems = &mp_Items;
      decltype(pItems) Aliases[] = {pItems};
      Aliases[0]->f_Clear();
    }
    void f_InArrayRead() // fine: the element is only read
    {
      auto *pItems = &mp_Items;
      decltype(pItems) Aliases[] = {pItems};
      (void)Aliases[0];
    }
  };

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
