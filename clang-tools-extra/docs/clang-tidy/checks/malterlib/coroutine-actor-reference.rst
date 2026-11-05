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
every resume with ``fg_OnResume``. A local of an ``fg_OnResume`` callback is
looked up each time the callback runs, and not kept from before the
suspension, only when it is automatic, made directly by a call of one of
:option:`CallbackLookupFunctions`, also ``operator[]``, not a static one,
which has no object, on a member of the
object, ``mp_Applications.f_FindEqual(_Key)``, ``this->mp_Items[_Key]`` or
``&mp_Items[_Key]``, or by what is reached from such a lookup again, a member
of what it gives, ``auto &Data = mp_Items[_Key].m_Data``, what an owning
pointer there owns, ``&mp_Items[_Key].m_pData->m_Value``, or a lookup in it,
also through a local of the callback that is one itself, ``auto &Children =
Entry.m_Children`` of ``auto &Entry = mp_Items[_Key]`` (not a copy of one,
``auto *pApplication = pFirst``, nor the value of a pointer stored in what
is looked up, ``auto *pParent = mp_Nodes[_Key].m_pParent``, nor through a
pointer or a reference the object keeps,
``mp_rCached.f_FindEqual(_Key)``,
``m_pCached->f_FindEqual(_Key)``, nor through an owning pointer of the object
itself), and only read after, also in parentheses: never assigned,
``(pApplication) = ...``, incremented, cast, given by its address or by a
reference, also to const, bound to a reference, or captured. Anything else,
like a copy of another local or what another function gives,
``fg_Identity(pSaved)``, is kept from before. What the callback assigns what
it re-validates re-validates it only when it is such a call too. A pointer or reference into the data is
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
``mp_Items.f_Clear()``, also one with an explicit object parameter, also an
operator, ``mp_Items()``, also called by name, ``mp_Items.operator()()``,
which removes by its name, or, on the collection itself, also a member of it
or through an alias, a reference or a pointer to it, not an entry of it,
``mp_Items[0].f_Update()``, which changes the entry, and not a lookup, one of
:option:`CallbackLookupFunctions` or ``operator[]``, which is told by its
name only, whose body changes its object, looked into
like the body of a helper given the collection, with its object, ``this``
or the explicit object parameter, as the collection, to a depth: calls one
that does on it, also on a member of it, ``m_Storage.f_Clear()``, or through
an iterator made from it, also through a local reference or pointer to it,
or one given by a pointer, ``f_GetIteratorPointer()->f_Remove()``, which
removes from the collection it is in,
assigns it, also compound, whatever the operator's qualifiers, ``*this =
CItems{}``, moves it, ``fg_Move(*this)``, also cast to an rvalue reference or
by a member function qualified ``&&``, also one whose explicit object
parameter is an rvalue reference, ``f_Drain(this CItems &&)``, also an
operator, gives it on to another function or a
constructor, not as const, also a pointer to it by a reference, also to a
const pointer, ``CItems *const &``, not one moved or forwarded,
``fg_Move(pSelf)``, also in the body of a lambda there, called or not, of a
generic one also the bodies it is instantiated with, and a
call operator without a body to look into, also any other operator without
one in such a body; the other arguments of one with an explicit object
parameter are those of a helper; a member function of the actor itself,
``f_TidyOther()``, isn't a collection's, and is gone through for the
collections it changes; also a move of the collection itself,
``fg_Move(mp_Items).f_Entries().f_GetIteratorDestructive()``, which empties
it, also of one that is a member of one of the object, counted against that
one, ``static_cast<CItems &&>(mp_Wrapper.m_Items)``, also through a local
alias of the object, ``pThis->mp_Items``, also an
assignment of the collection itself, also through an alias or a reference to
it, not of an entry of it, ``*mp_Items.f_FindEqual(1) = 42``, nor of an
iterator made from it, also by an ``operator=`` called by name, also
compound, whatever the operator's qualifiers, also
with an explicit object, ``mp_Items.operator=(CItems{})``, also one that
calls such a member function on its object, also with an explicit object, also in a scope
exit it makes, ``g_OnScopeExit /
[this] { mp_Items.f_Clear(); }``, but not in a branch a constant condition
never takes, also through a free helper it gives the collection to, whose
body is looked into, not the bodies of what it calls, also through a local
iterator made from the collection there, also by a member function with an
explicit object parameter, where a member function
called on the collection, also an operator, also through a pointer to it,
``pItems->f_Insert(1)``, of the collection's type, is told the same as in the
callback, and deleting what is reached from it, also through what a member
function of it gives by pointer or reference, ``delete _Items[0]``, also cast
or as either value of a conditional, ``delete static_cast<CDerived
*>(_Items[0])``, also through pointer arithmetic or as the right of a comma,
``delete (_Items[0] + 0)``, removes,
also in the body of a lambda it makes, called or not, also through
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
``mp_Values.front()``, lives with the actor, also in a loop over it, when
the actor holds the array itself by value, not one in an entry,
``mp_Items[0].m_Fixed.data()``, which dies with the entry, nor one a pointer
member points to, ``m_pValues->data()``, which can be replaced. A reference bound to a temporary copy,
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

What the callback, a helper given a collection or a member function of
a collection does to an expression, what a lookup points into and which
member it goes through first, and whether an expression reaches into the
object of the actor at all are told by what the expression is relative to
the object (the actor, the collection given or the collection whose member
function it is), the same way everywhere:

- the object itself: through parentheses, any cast, also to an rvalue
  reference, also to a number and back, ``reinterpret_cast<int *>(
  reinterpret_cast<uintptr_t>(&m_Value))``, also through a comma or a
  conditional of numbers, or stepped, ``sizeof(int) +
  reinterpret_cast<uintptr_t>(m_Values)``, a temporary or cleanups around
  it, ``&`` and ``*``, also ``std::addressof`` and ``__builtin_addressof``,
  the right
  of a comma, ``fg_Move`` or ``fg_Forward`` of it, both values of a
  conditional when both are, ``(bChoose ? mp_Items : mp_Items)``, or the
  other when one is null, ``bChoose ? this : nullptr``, what a lambda called
  in place or a statement expression gives back, what an accessor of it
  gives that returns a reference to its own class, ``*this``, also an
  operator, ``mp_Items()``, also a subscript, ``CItems &operator[](int) {
  return *this; }``, also ``operator*``, ``operator->`` or ``get()`` of what
  is like a pointer, before what an owning pointer owns, also one whose
  body can't be seen that returns a reference to its own class, ``CItems
  &operator[](int)``, before what its kind tells, or one with an
  explicit object parameter, or a
  pointer to it, ``this``, also through a local alias or an address,
  ``auto &Self = *this; return Self;``, ``&*this`` or ``&_Self``, as what
  each return of its body is relative to its object tells, when its body
  can be seen, or whatever it returns when it can't, ``mp_Items.f_Entries()``,
  unless an override of a virtual one can give something else, which can't
  be told, what ``operator*`` or ``operator->`` of it gives, any operator
  called by name the same as written as one, ``mp_pItems.operator*()``, or a
  local reference or pointer to it; a pointer assigned again, also the copy
  a lambda captures, is nothing of the object when what it was last pointed
  to before the use isn't, ``pItems = &Local``, and else what can't be told,
  through a pointer or a copy when what it was pointed to is;
- a member of it, also of a member, ``mp_Wrapper.m_Items``, also through a
  pointer to a member, ``this->*pmItems``, or an element of a fixed-size
  array member, a built-in array or one of :option:`FixedArrayTypes`, by
  subscript, ``at``, ``front``, ``back``, ``data`` or the iterators, ``begin``,
  ``end`` and their kinds, ``mp_Items[0]`` of a
  ``std::array``, counted against the outermost, the collection, also what a
  member function whose body returns a member of its object gives,
  ``CChildren &f_Children() { return m_Children; }``; also a
  pointer member itself, ``&m_pOther``, also through a local reference to
  it, which is part of the object (not a local pointer, ``&pItems``, whose
  address is of the local), and what is reached through what a pointer or
  reference member points to, ``m_pOther->m_Items``, also through a local
  reference to it, or an owning
  pointer to an actor, counted against that member, though it can be another
  object, which the checks that ask whether something is the object itself
  or held by it by value don't count (:doc:`malterlib-actor-phase
  <actor-phase>`, :doc:`malterlib-actor-dispatch-this <actor-dispatch-this>`);
- an entry of it: an element, ``mp_Items[0]``, also through pointer
  arithmetic, what a member function of it gives, ``mp_Items.f_Get(0)``,
  also an iterator or a handle by value, or a local reference, pointer or
  iterator made from one, ``auto iEntry = mp_Items.f_GetIterator()``, also
  an iterator that an iterator gives, also a copy, ``iEntry.f_GetIterator()``,
  which is at the same entry, as
  deep as the collections it is in, ``mp_Items[0].m_Children[0]`` two deep;
  a value made from what it is in that can refer back, other than an
  iterator, which refers to an entry, like a view or a handle,
  ``mp_Items.f_View()``, can refer to that itself, and a change through it is
  counted as deep as that;
- unknown when it is made of the object in any other way, like a conditional
  of the collection and something else, also of a member and an entry,
  ``(bChoose ? mp_Union.m_Count : mp_Items[0].m_Value)``, or what a function given the
  collection by reference or pointer returns by reference or pointer,
  ``fg_Pick(mp_Items)``, which counts as all of the object (one whose body
  can be seen gives what each return gives: a parameter that isn't assigned
  again, also its address, the argument, ``fg_Const(mp_Items)``, of a pointer
  given by reference and given back as its value, what it points to, what is
  made of no local, parameter or object, like a global, nothing, an
  alternative kept as such, and else, like what a local alias of a parameter
  gives, what can't be told);
- nothing of it: what ``typeid`` gives, ``typeid(*this)``, the information of
  a type, a value that is a number, ``mp_Items.f_GetLen() + 1``,
  unless it is made of a pointer, also by a bit-cast, what makes another
  object, and a copy, a value a member function makes of it that can't refer
  back to it, ``mp_Items.f_Copy()``, whose entries are its own, which no
  change is counted of; a value that can, of ``ViewTypes``, an iterator, or a
  class with a pointer, reference, view or iterator member, also of a base or
  of a member it holds, or one like a pointer, with ``operator->`` or
  ``operator*``, like a ``TCBitStorePointer``, also what a subscript gives,
  ``mp_Items.f_View()``, is what is in it. Each of
  these, a member of a union, through a pointer, a copy, is kept however
  what it is in is merged or gone further into, as either value of a
  conditional, either way round, the right of a comma, a cast, a local alias,
  a pointer assigned again, a member of it or a helper, also one whose
  return can't be told, also a lambda called,
  and the value of a pointer that a conditional chooses is told of each value
  by itself, ``b ? pLocal : m_pOther``, or of the one a constant condition
  chooses; what an owning pointer points to is stored by itself, not in a
  union the owning pointer is in, and is one deeper than the owning
  pointer, which can end it without being ended itself, however the owner
  is reached, a member, an entry itself, ``mp_Owners[0]``, an accessor,
  ``f_Owner()``, a local alias, a cast or a helper that gives it back, not
  what an iterator or a handle made from what it is in refers to, which is
  that itself. How deep each is, like each flag, is kept however it is
  reached: through an accessor whose body can be seen, as deep as what it
  returns, an entry of an entry, ``return
  (*this)[0].m_Children.f_FindEqual(0);``, at least that deep for a lookup and
  at most one deeper for a change through it, what an owning pointer of it
  owns, ``return m_pData.f_Get();``, and a pointer it holds, like the storage
  of a container, ``return m_pStorage;``, an entry of it, not another object;
  and a pointer stepped where it is,
  ``++m_pOther`` or ``m_pOther += 1``, points where it pointed, through what
  holds it. A local pointer's address, ``&pItems`` or
  ``std::addressof(pItems)``, also through a local reference to it or a cast
  to a reference to it, ``&static_cast<int *&>(pItems)``, is of the local, and
  a local reference to a local pointer, ``auto &rpItems = pItems``, reads the
  pointer where it is used, ``pItems = &mp_Items; rpItems->f_Clear()``,
  and what it points to again, ``*ppItems`` of ``auto ppItems = &pItems``, is
  the pointer, which points where it does; given to a helper, also through
  such a local, ``fg_Clear(ppItems)``, the helper can follow it to what the
  pointer points to. Where a pointer is held that can't be told within what is
  followed is what can't be told. An element through a pointer an object
  holds, ``m_pData[0]``, is an entry, not a subobject. What a value made by
  a call can refer back to, like a view or a wrapper of a pointer,
  ``fg_View(_Items)``, can't be told of what it is made from, and so is an
  object made of what it is given by pointer or reference, or of a value
  that can refer back, ``CView{&_Items}`` or ``TCRef<CItems>(_Items)``, also
  a local one, ``auto View = fg_View(_Items)``, an array of pointers,
  ``CItems *Batch[] = {&_Items}``, and what a local is given later where
  that runs, ``View.m_pItems = &_Items``. A copy is new at its own level, a
  copy of a class, made or given back by value, unless it only refers, a
  view of pointers, a view of ViewTypes, an iterator or a pointer wrapper:
  its storage, its entries and its members held by value are its own, also
  of a mixed one, which nothing of the actor changes and using which isn't
  using the actor's data, ``Copy.m_Value``, ``Copy.m_Scratch.f_Clear()``;
  what its borrowed parts lead to, references, pointers that aren't storage
  a collection owns, and what an owner of :option:`SharedPointerTypes` points
  to, is the source's, one level down, ``Copy.m_pItems->f_Clear()``,
  ``Copy[0]->m_Value`` of a copied collection of pointers, also through an
  accessor that gives a borrowed member, ``Copy.f_Items()``. Storage a
  collection owns is that held by a pointer of it, or of a class in it,
  ``CStaticData`` of ``TCVector``, to what it holds, a type it is given, a
  class in it, a node of its types or bytes, not a pointer to anything else,
  ``int *m_pCounter``, nor of what is like a pointer, ``TCUniquePointer``,
  also given by a member function of what holds it, and is an entry of it,
  which a removal frees apart from it. What holds a value as bytes, an
  optional or a variant, has the borrowed parts of the types it is given. A
  member function called on a copy, or a copy given to a helper by value, is
  gone through as by a destructor, for what it changes through its borrowed
  parts, also one with an explicit object parameter, ``f_Tidy(this CView
  &_Self)``, and so is a removal by name, ``Copy.f_Clear()``, which of one
  whose body can't be seen removes only the copy's own, and a helper given a
  view or a wrapper by value or to const, ``fg_Clear(CView const &_View)``,
  whose body can be seen; a copy keeps what its source's borrowed parts lead
  to. A callback that captures a copy uses the
  actor's data only through its borrowed parts, ``*Entry.m_pPeer``, also
  through a member function or an operator of it that can reach one, an
  accessor, ``Entry.f_Peer()``, also with an explicit object parameter,
  ``operator()(this CEntry const &_Self)``, or one whose body can't be seen,
  or when it gives the copy anywhere else, not where it isn't evaluated,
  ``sizeof(m_pPeer)``; this is told for each callback by itself.
  What a copy of a shared owner points to is kept alive by
  it while it is used through the copy, not once a pointer or a reference
  holds it, so using it isn't using what can be erased, though what is looked
  up in it, or looped over, is, ``pChildren->f_FindEqual(0)``, and changing it
  changes what the source points to; destroying the copy, also one a
  destroyed object holds, releases it only, for the source owns it still,
  unlike one moved from the source, ``fg_Move(mp_pItems)``. What a unique
  owner moved into a new one owns is that one's alone, ``auto pState =
  fg_Move(mp_pState)``, what it points to by pointers the source's.
  A collection of pointers, or of what destroying changes nothing of, whose
  destructor can't be seen, destroys only its own storage and elements.
  Independent is what has nothing below its own level:
  plain value data, an owning pointer that can't be copied of what is
  independent, or a collection of independent elements without references
  or pointers to other collections, also as members at any depth,
  ``TCVector<int>``, whatever its member functions are named; and so is a
  value an await gives, ``auto Release = co_await fp_GetRelease()``, unless
  it only refers, a view or a range, ``co_await f_RangeAsync()``. A local
  that is given a borrowed pointer later, ``Local.m_pItems = &mp_Items``,
  keeps its own storage as its own. A
  structured binding is the part it names. And what is
  reached through a pointer an object holds, which can point back to what
  holds it, ``m_pOwner->m_Items``, changes what can't be told.

A collection is changed by a removing call on it, also on a member of it or
through what can't be told, by assigning it, also through a pointer to it,
``pItems->operator=(...)``, by an assembler statement it is an output of, or
a pointer to it an input of, ``asm volatile("" : "+m"(_Items))``, and by
Microsoft's ``__asm``, which can write anything, by an atomic operation on it
other than a load, ``__atomic_store_n(&_Items.m_Count, 0,
__ATOMIC_SEQ_CST)``, by assigning any member of a union of it, which
can end the life of another, also anything in a member of it at any depth,
also compound and through casts, ``_Items.m_Entry.m_Value += 1``, also
through a local reference to one or a helper given one, ``int &Slot =
mp_Items.m_Empty; Slot = 0``, also through a pointer to a data member of one, ``(mp_Items[0].*pmEmpty)
= 0``, also one that can't be seen of a union or a class with one, also in a
base, by assigning its own state, ``_Items.m_nSize = 0``, also a pointer it
holds, ``_Items.m_pData = nullptr``, also through a reference parameter
given it, ``void fg_Reset(int *&_pSlot) { _pSlot = nullptr; }``, also in a
union (not a local pointer pointed elsewhere,
also one into a union, ``pSlot = &Other``),
also of a collection in an entry, by moving it or deleting
what is reached from it, ``delete mp_Items[0].m_pNext``, by a member function
whose body does, also one that removes by name, whose body can also end its
object, ``void f_Remove() { delete this; }``, or one without a body to look
into that is an operator, by a
virtual member function that an override can run whose body isn't seen, also
of an entry that holds a collection, which changes that,
also a const one, unless it is a lookup, told by its name, or called on what
can only be of its class, a variable or a member that isn't a reference, by a
const one whose body changes it, ``const_cast<CItems *>(this)->f_Clear()``
(one whose body can't be seen doesn't, unless it is a call operator), by
giving the address of a local pointer to it on, ``fg_Clear(&pItems)``, and by
a call
through a pointer to a member function, told by the name of the one it can be
seen to point to, also through a cast that picks one of overloads,
``static_cast<void (CItems::*)()>(&CItems::f_Touch)``, also one that moves its entries, an assignment when it is
an assignment operator, and as unknown when that
is a virtual one an override can run, or one a lambda can point elsewhere,
also its copy in a mutable one, else an unknown change. A collection in an entry of it, also given to a
helper, ``fg_Clear(mp_Items[0].m_Children)``, also one whose entries move,
``mp_Items[0].m_Children.f_Reserve(8)``, is gone through the same way, counted
against the member the entry is in, and so is an entry given to a helper or
whose member function is called: what changes a collection in it counts, a
removing call, ``mp_Items[0].m_Children.f_Remove(0)``, a delete, and an
assignment or a move of a collection, ``m_Children = {}``, not of plain data,
``m_Value = 1``, but an assignment or a move of one that holds a collection,
``mp_Items[0] = CEntry{}`` or ``fg_Move(mp_Items[0])``, also in a member
function of it, ``fg_Move(*this)``, also cast to an rvalue reference, which
replaces or moves from that collection, counted against the member the entry
is in like a change of that collection, which changes what is looked up in
that collection, not the entry itself, also an entry given on, not as const,
also through a pointer to it, ``fg_Change(&mp_Items[0])`` or
``fg_Change(this)`` in a member function of it, to what can change it,
``fg_Change(mp_Items[0])``, also in a helper's body, also one that holds an
owning pointer, also the owning pointer itself; given to one whose body can
be seen, what that body changes of it, and to one that can't be seen by a
pointer or a reference to a class, also what is given itself, which it can
delete or deallocate, ``fg_Delete(mp_Items.f_FindEqual(0))``,
``free(mp_Items.f_FindEqual(0))`` or what a pointer of an entry points to,
``fg_Delete(mp_Items[0].m_pValue)``, not one by a reference to a number,
``fg_Set(mp_Items[0].m_Value)``, and an assignment of an
owning pointer it holds, ``mp_Items[0].m_pItems = {}``, also of what holds
one at any depth, ``mp_Items[0].m_Holder = {}`` or ``mp_Items[0] = {}``, or a
removing call on it, ``mp_Items[0].m_pItems.f_Clear()``, which ends what it
owns, whatever that is; a change of a member of a union in an entry, also
replacing what holds the union, ``mp_Items[0].m_State = CState{}``,
or one that can't be seen of an entry with a union in it, also given on, can
end a member of it, which counts as a change of the entry itself, not a
move of it, which ends no member; a number
moved, ``fg_Move(m_Count)``, changes nothing; what a call that consumes its
object is given is given to it like to any other; what a helper's body or a
member function's body changes counts as deep as the shallowest of it; a move
of an entry that holds none changes only the entry, nor of an iterator,
``*this = CIterator{...}``, which steps the iterator, not the collection it
was made from. A collection is a class with a member function of
``RemovingMethods`` (:doc:`/clang-tidy/MalterlibOptionsFile`), not an
iterator. What holds a collection holds one by value, also in a base or a
member it holds, or owns one through an owning pointer, a class, not an
iterator, with ``operator->`` or ``operator*``, also inherited, also an
instantiation of a member template, whose pointee
is or holds one, at any depth; a type that owns itself, ``struct CNode {
TCUniquePointer<CNode> m_pNext; }``, also through others, is gone through
once. The arguments of a member operator after its object, also of a call
operator, ``CWorker{}(mp_Items)``, and of a free operator are given to it like
those of a helper. What isn't evaluated neither changes nor uses anything:
the operand of ``sizeof``, ``alignof`` and the other traits, but the size of
a variable length array, of ``noexcept``, of a ``typeid`` that doesn't look at
the dynamic type, of a type trait, a requires expression or a concept, and
the arguments of a builtin that clang doesn't evaluate, like
``__builtin_constant_p(&m_Count)``, ``__builtin_object_size`` or
``__builtin_classify_type``, but ``__builtin_dynamic_object_size``, also what
``__builtin_assume`` is given.

A helper's body, also of a member function, a constructor or a destructor,
is gone through by everything that runs in it, as its control flow lists it,
also where an exception is caught: its statements, the member and base
initializers of a constructor, ``CReset(CItems &_Items) : m_Dummy((_Items.f_Clear(),
0)) {}``, also the constructor an inherited one runs, ``using
CClearing::CClearing;``, the allocation function of ``new``, and what runs implicitly, the
destructors of locals, also arrays of them, temporaries, also one whose life
a reference extends, members and bases, and the cleanup functions of
variables; each call there is told the same way, its body when it can be
seen, else as one that can't be, also in the callback, where a constructor
and a local or temporary destroyed are too. Of an object destroyed, what it
keeps is each value its whole initialization binds to a pointer or a
reference to what isn't const, also a reference to such a pointer, as a
parameter of a constructor, a factory or a member operator, or as a member of
an aggregate, each initializer to the member it initializes, the selected
member of a union, ``{.m_pItems = &_Items}``, each element of an array, not
an unnamed bit-field, through its bases, members, array elements and
fillers, in braces or in parentheses, and the object of a member function
that makes it, ``_Items.f_MakeGuard()``, not what makes a value that can't
refer back to it, a number or a value all its own, ``CText
Text(pEntry->f_GetLen())``, one that owns what it holds, a collection or an
owning pointer that can't be copied of what can't refer back, a value
independent of what it is made from, also copied from one not as const,
``TCVector(TCVector &)``, or by a free function, ``fg_TempCopy(mp_Values)``,
whatever its member functions are named, or one the body that runs
makes of nothing related to its object or its parameters, as what an
expression is relative to them tells, through local aliases, views and
copies, ``return CPrinted{};``, not ``auto &Items = *this; return
CView{&Items};``, not a copy, nor what a lambda in it
captures; also, of a local, what it is given after it is made, assigned to
it or a member of it, ``Guard.m_pItems = &_Items``, also through a local
alias of it, ``auto &Alias = Guard`` or ``auto *pGuard = &Guard``, or given
to a call besides it, each argument as it is bound, whatever the call gives,
``Guard.f_Set(_Items)`` or ``fg_Set(Guard.m_pItems, _Items)``, unless the
call's body can be seen, no
override can run instead, and it never names the parameter, where that runs,
not where it isn't evaluated or never runs, ``if constexpr (false)``, nor in
a lambda never called, one discarded or a local never named, ``auto fNever
= [&] { Guard.m_pItems = &mp_Items; };``; what
its destructor changes of its own state, what it holds by value, doesn't
count, and what it changes through what it refers to, a pointer or a
reference it holds, what it owns, also through a wrapper's ``operator->``,
``m_pItems->f_Clear()``, also in a member function it calls,
``~CGuard() { f_Cleanup(); }``, or what can't be told, counts against each
value it keeps, as deep as that, with no telling which member keeps which
(the union of what it changes counts against each),
of entries as of an entry, whose plain data it can restore, ``m_rValue =
m_Saved``;
one that can't be seen can end what it keeps. A cleanup function,
``__attribute__((cleanup(fg_Free)))``, is given the address of the variable
with any value it is given, its initializer or one it is assigned, as any
can be what it holds where its scope ends, also by an early return; of a
pointer, pointing it elsewhere, ``*_ppItems = nullptr``, changes nothing of
what it pointed to. An explicit destructor call, ``_Items.~CItems()``, ends
its object, and so can a removing member function whose body can't be seen,
``mp_Items[0].f_Remove()``, unless what it is called on is part of another
object, a member, also through an accessor, a cast, an element of an array
member, ``m_Children[0]``, a local reference or an unchanged local pointer
to one, not what a pointer member points to,
``mp_Items[0].m_pHeap->f_Remove()``, or an iterator; in a helper's body,
what its object or a parameter is, as its caller gives it, so ``void
f_Tidy() { f_Remove(); }`` called on an entry can end the entry. A call that can run an override (a virtual member function that
isn't final, not called by its name, on what isn't an object of a type known
exactly, also as an operator, ``mp_Items()``) is one whose body can't be seen,
which can end what it is given and what it is called on, also a removing one,
whose override can ``delete this``; one on a final class or member, called by
its name, ``_Processor.CProcessor::f_Apply(_Items)``, or on a variable or a
member of the class, keeps its body, when that is the one that runs: one a
class names through a ``using`` of a base's, ``using CBase::f_Selected;``,
whose override in between runs, is one whose body can't be seen. The body of
any call is only read when it runs. An assignment operator written for a
class, ``m_Registration = CRegistration{}``, is gone through like a member
function, also of a part of an entry; past the depth the helpers are gone
through to, a member function can end its object. A helper given the
object of the actor that only names members of it, ``_pThis->mp_Other``,
changes only those members. What a helper lets escape, a pointer to
what it is given kept in a global or a static, in another object, or in what a
parameter or a local refers to or points to, ``_Slot.m_pItems = &_Items``,
``g_pSaved = &_Items``, also through a helper it calls, can be ended later
through that, as by what can't be seen; not one kept in a local itself, also
an element of a local array, ``Batch[0] = _Items.f_FindEqual(0)``, also
through a local reference or an unchanged local pointer to a local,
``auto &Alias = View; Alias.m_pItems = &_Items``, given
back through a parameter itself, ``_pValue = _Items.f_FindEqual(1)``, held
by the object itself, or by the object a constructor makes, ``m_pItems =
&_Items``, whose destruction tells what it does with it. A write into a
local, ``View.m_pItems = &_Items``, changes none of what it can refer to;
a member function called on a local is gone through as by its destructor,
for what it changes through what it refers to. Given the address of a local pointer, what the helper
changes of that pointer, ``*_ppItems = &g_Other``, points it elsewhere, not
a change of what it pointed to, and so does a local pointer given by
reference, ``o_pItems = &_Items`` of ``fg_Set(pItems, _Items)``, also through
a local reference to it, also in the callback, which then points where the
setter points it, where the setter runs. A pointer given by a reference, ``int *&``,
also a const one, ``CItems *const &``, or one to const, ``CItems const *&``,
to what can't be seen can be deleted through it, and to one that can be seen
is gone through as any other, ``delete _pItems; _pItems = nullptr;``. A member function called through a pointer to one qualified
``&&``, ``(_Items.*fDrain)()``, consumes its object. An iterator assigned
again is at what it was last assigned, or anywhere when that can't be told;
one only stepped stays in what it was made from. A generic lambda is gone
through as it is instantiated, not as written. What an operator gives by
value that can refer back to its object, like a view, ``(_Items +
0).f_Remove(1)``, is what is in it, as of a named member function, and what
an accessor whose body can be seen gives that can't be told, ``return _bOwn ?
*this : g_Other;``, can be the object itself, also given to a helper; what
it gives of the object is of the member it reaches, ``return m_Other;``, and
can be null when that can, ``return bKeep ? this : nullptr;``,
``fg_Clear(mp_Items.f_Select(bOwn))``, and what a lookup gives that can't be
told, like a node found in a tree of a ``TCMap``, ``return
&pData->f_Value();``, is an entry of it, also given to a helper,
``fg_Delete(mp_Items.f_FindEqual(0))``. A local pointer given by a reference
to it, ``fg_Refresh(pEntry)``, is looked up again, not ended, by one that can't
be seen.

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
``std::array``, which can't lose them, nor over what the coroutine holds
itself: a list made where the loop is, ``{&m_Client, &m_UDP}``, a local
array, also a member of a local held by value, or a copy at its own level,
``auto DebugManagers = mp_DebugManagers.m_Actors``, not a local wrapper that
refers to the actor's collection, whatever its member functions are named.
Loop over a copy, or collect what to do before suspending.

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
pointer or a virtual function that isn't final, can. What a block calls before
what the analyzer takes as not returning can't free what is used after the
call only for the shape of the failure branch of ``DMibFastCheck``, a block
that can't be entered other than at its start, by a label or a ``case`` in it: each
statement before it, also in a block or a ``do { ... } while (false)``, is an
assembler statement without operands that isn't an ``asm goto``, like the trap
``__asm__ ("int $3")``, not Microsoft's ``__asm``, whose jumps can leave, or
a call of a function named directly, not one an expression gives, known not to
throw (``noexcept``, trivial, a ``nothrow``
builtin like ``__builtin_debugtrap``, or one whose body in the translation unit
is empty, like the constructor of ``CNoReturn``) whose arguments, also default
ones, are literals, variables, members of them or their addresses, not a
``thread_local`` variable that isn't initialized with a constant, also a
binding of a structured binding that is one, also any binding of one of a
tuple-like type, whose own variable calls ``get<...>()``, whose
initializer its first use in a thread runs, which can throw, and what
doesn't return is such a call, or an automatic variable, not ``static`` or
``constexpr``, whose constructor is, ``CNoReturn NoReturn;``, not an array
of none, ``CNoReturn Stops[0];``, which constructs nothing. Any other block can go on, like one that can return or throw
first, makes an object whose destructor can throw, or whose call that doesn't
return can throw, like ``fg_MibSafeCheck``.
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
``(void)CStop{this}``; in a destructor, ``this`` and what is reached from it,
also through accessors, ``f_Held().f_App().f_StopApp()``, is the object
destroyed, not the own actor) (not what a lambda doesn't run, like in a branch of an
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

.. option:: CallbackLookupFunctions

   Semicolon-separated list of the member functions whose calls on a member
   of the object look up again each time an ``fg_OnResume`` callback runs, so
   a local of the callback made by one isn't kept from before the suspension.
   Also one declared with an explicit object parameter, a plain call. Built in
   is `operator[]`; Malterlib's options file adds `f_FindEqual`.

.. option:: FixedArrayTypes

   Semicolon-separated list of classes that hold their elements in them,
   fixed in number, like a built-in array, whose elements are parts of what
   holds them, not entries that can be removed, a list the checks share
   (:doc:`/clang-tidy/MalterlibOptionsFile`). Built in is `std::array`.

.. option:: SharedPointerTypes

   Semicolon-separated list of owning pointers whose copies share what they
   point to, counting references, a list the checks share
   (:doc:`/clang-tidy/MalterlibOptionsFile`). Built in are `std::shared_ptr`
   and `std::weak_ptr`; the Malterlib options file adds Malterlib's, like
   `TCSharedPointer` and `TCWeakPointer`.

.. option:: NodeStableContainers

   Semicolon-separated list of containers whose entries stay where they are
   when others are added or removed. Built in are those of the standard library, `std::map;std::set;std::multimap;std::multiset;std::list;std::forward_list`, and the Malterlib options file (:doc:`/clang-tidy/MalterlibOptionsFile`) adds Malterlib's, like `TCMap`.
