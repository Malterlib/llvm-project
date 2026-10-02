.. title:: clang-tidy - malterlib-parameter-ownership

malterlib-parameter-ownership
=============================

Finds parameters of non-coroutine functions that take an owning object by
value, also of a defaulted comparison, ``bool operator==(COwner _First,
COwner _Second) = default``, also one that nothing uses. Such a parameter takes ``const &`` when the function borrows the object,
and ``&&`` when it consumes it, by moving from it where the move runs (not
in ``noexcept(std::move(_Value))`` or a discarded branch of an ``if
constexpr``). A caller that has to pass an lvalue to a
consuming parameter makes the copy explicit with ``fg_TempCopy``.

.. example-before
   struct CStr
   {
     CStr();
     CStr(CStr const &_Other);
     ~CStr() { delete[] m_pData; }
     char *m_pData = nullptr;
   };
   void fg_Use(CStr const &_Text);
   void fg_Keep(CStr &&_Text);

.. code-block:: c++

  void fg_Print(CStr _Text) { fg_Use(_Text); }                    // warning
  void fg_Print(CStr const &_Text) { fg_Use(_Text); }             // fine: borrows
  void fg_Store(CStr &&_Text) { fg_Keep(static_cast<CStr &&>(_Text)); } // fine: consumes

An owning object is an object of a class that releases something when it is
destroyed: its destructor releases something, like ``delete m_pData``, or
uses the object, like ``m_Data.f_Clear()`` or ``fg_Release(m_pData)``, also
through a local made from it, also a structured binding of it, ``auto
&[pData] = *this``, also destroying what it holds explicitly,
``reinterpret_cast<CType *>(m_Data)->~CType()`` (what destroying that type
releases), also a guard or a
temporary whose destructor
releases what it is given, ``CGuard{pData}``, also through a member function
of it that its destructor calls, ``~CGuard() { f_Clear(); }`` (in a member its destructor
uses, when it can be seen, like for a local), also given to a member of it
after it is made, ``Guard.m_pData = m_pData`` (in a member its destructor
uses, when it can be seen), also what a member function returns made of it,
``CGuard f_Release() { return {m_pData}; }``, also in a helper it is given
to, also what a helper returns of what it is given on any of its returns,
``delete fg_Data(m_pData)`` with an early ``return nullptr;``, (but not a scratch copy, also one a helper that
only returns new allocations makes, ``delete fg_Copy(_pData)``, also a local copy of a value
written through its address, ``int Value = *m_pData; fg_Reset(&Value)``, or
given by reference, also of a class that holds only values, ``CState State =
*m_pState; fg_Normalize(State)``, also a member of it, ``fg_Normalize(
State.m_Value)``, also given to a function object
or captured by a lambda, ``[pScratch = new int(*m_pData)] { delete pScratch; }()``,
also from a local by a lambda called where it is made, ``[pScratch] { delete
pScratch; }()``, or kept in a local that is only called, ``auto fCleanup =
[pScratch] { delete pScratch; }; fCleanup();``,
``CGuard{new int(*m_pData)}``, also one given it into members its destructor
doesn't use, ``CGuard Guard(m_pData, new int(0))`` with ``~CGuard() { delete
m_pScratch; }``, nor a lambda given to a helper whose body can be seen not to
use it, ``fg_Ignore([this] { delete m_pData; })``, or only where it isn't
evaluated, ``noexcept(_fWork())``, or only copied into a lambda it makes and
doesn't call, ``return [_fWork] { _fWork(); };``, unless what it returns is
called, nor a copy that the class
of it makes itself,
like a deep copy, ``COwner Scratch(*m_pOwner)``, also through a base it
constructs, ``CStr(CStr const &_Str) : CImp(_Str) {}``, also a copy of what is const,
``TCSharedPointer<int> pSnapshot(*m_pPointer)``, which takes a reference of
its own, but not one that takes what it copies, a copy constructor of what
isn't const) (only checking it, like ``if (!m_pData)
fg_Fail()`` or giving it to a parameter that is a pointer or reference to
const or to a const member function (of an operator with an explicit object,
``operator!(this CType const &_Self)``, the object is its parameter), doesn't,
unless what it is given to
releases it anyway, which is looked into when it is defined in the file, also
through what that calls on it, like a virtual one that isn't final, also a
const one, ``_pResource->f_Release()``, but not a const one that gives back an
owner it makes, a weak pointer locked, ``_pWeak.f_Lock()``, nor
does a member function that isn't const whose body only checks it (not a
virtual one that isn't final, ``m_pResource->f_Release()``, whose override can
release), or a number
given by value, unless what it is given to releases what it identifies, like
``fg_Close(m_Handle)`` (not a member of an instantiation another file makes,
``extern template``, whose instantiation there tells what it does), also a handle copied by value, unless what it is given
to can be seen not to release it, ``fg_Log(m_Handle)``, also as a variadic
argument of a function that can be looked into and doesn't read them, while a
statement of the destructor that calls a function given nothing, like the
``fg_Unlock()`` of a scope guard (one whose body can be seen releases what it
does in it, also through a chain of them, however long, also what it returns, ``return
fg_Unlock();``, and one that only returns a
constant releases nothing, nor does one that doesn't return, like the
``[[noreturn]] fg_Fail()`` of a failed check), a member function of a global that isn't const and
releases what it has, ``g_Resource.f_Release()``, or a
function given more that can be seen to do so, ``fg_Release(m_bActive)``, releases what the thread or program has
(also one that only changes what the program has, like a count of
destructions, ``++g_nDestroyed``, also atomically,
``__atomic_fetch_sub(&g_nUsers, 1, __ATOMIC_RELAXED)``, which a copy changes again),
also in parentheses or cast to void, ``(fg_Unlock());``, also in an
argument of another call, ``fg_Work((fg_Unlock(), 0))``,
also in a branch, ``if (m_bActive) fg_Unlock();``, or a loop, ``while
(m_nLocks--) fg_Unlock();``, of a class that defines or deletes its copy
constructor, but not in a branch of an ``if constexpr`` that is discarded,
nor after a statement that always leaves there, ``if constexpr (!t_bLocks)
return;``, and through what a member function of a member that it calls does,
``m_fCleanup()``,
unless it can be seen to do nothing,
``fg_Log("View", m_pData)``, nor the object given to
what only changes its own members, or the address of a member, also an
array, that it only writes, ``fg_Reset(&m_pData)`` with ``*_ppData =
nullptr``, also the member given to a reference, ``fg_Reset(m_pData)`` with
``_pData = nullptr`` of ``int *&_pData``, ``fg_Reset(this)`` with ``_pView->m_pData =
nullptr``, also ``(*_pView).m_pData`` or through an unchanged local pointer or
reference made from it; where such a helper's write lands is what the places
of its target are in the helper, so an element of an array member of what it
is given, ``_Text.m_Data[0] = 0`` of ``fg_Clear(*this)``, is the object's own,
while an element of a pointer member of it, ``_Text.m_pData[0]``, or what a
reference member of it refers to, is past it. A library function the compiler knows, which has no
body to look into, does what one table tells: ``memset`` and ``memcpy`` write
only what they are given, so clearing the object's own bytes,
``memset(m_Bytes, 0, sizeof(m_Bytes))``, also through a helper,
``fg_SecureMemClear(m_Bytes, sizeof(m_Bytes))``, or the whole object,
``fg_SecureMemClear(*this)`` or ``memset(this, 0, sizeof(*this))``, also
through a getter whose body can be seen, ``fg_SecureMemClear(f_GetArray(),
sizeof(mp_Data))``, or a local pointer that holds it on every way there,
releases nothing, while
writing what a member points to, ``memset(m_pBytes, 0, 16)``, which the
copies share, does something, and ``free(m_pData)`` releases; ``memcmp``,
``strlen`` and other builtins without side effects only read, and fences and
hints, ``__atomic_signal_fence``, do nothing. An assembly statement does
something when it can reach what the program keeps, through its operands or
a ``memory`` clobber, unless its template is empty, ``__asm__ __volatile__(""
::: "memory")``; one with neither, a breakpoint, ``__asm__("int $3")``, does
nothing. A lambda called right away does what its body does,
also to what it is given, ``[](int *_pData) { delete _pData; }(m_pData)``,
what a lambda captures is of it, like the ``_pData`` of ``[_pData] { delete
_pData; }`` that a guard calls, also by an init capture, ``[pData = _pData]``, also through a copy of the lambda, also of
one held in a local the guard is given, ``TCGuard Guard{fCleanup}``, also one
held in a local that is called, ``auto fCopy = fRelease; fCopy();`` (which
calls it only there, so a scratch copy it releases isn't the object's), also
by name, ``fRelease.operator()()``, or
called through a pointer to it, ``(*pRelease)()``, and so is a pointer moved from a member, like
``delete[] (mp_pData - 1)``, also where it is, ``delete[] --mp_pData``, or kept encoded in an integer, ``close(m_FilePlusOne
- 1)``, or with tag bits,
``delete reinterpret_cast<int *>(m_Pointer & ~umint(1))``, or what a member function gives, like ``delete
f_Get()`` or a handle, ``fg_Close(f_Handle())``, and changing a count through
a subscript, ``--m_pCount[0]``, owns it like ``--*m_pCount`` (but not an
element of an array of its own, ``m_Cache[0] = 0``, also through a local
pointer to it, ``int *pCache = m_Cache``, also cast, ``*reinterpret_cast<uint8
*>(&m_Value) = 0``, or a range loop over it, ``for (int &Value : m_Cache)``), also through a local array made
from it, ``int *Pointers[] = {m_pData}; delete Pointers[0];``, also
``delete *Pointers``, or of what is
assigned to the element read, also the first one read through the array, ``Pointers[0] = m_pData`` (but what replaces it
before it is read, ``Pointers[0] = new int(*Pointers[0])``, is what it is, and
what it was assigned before that isn't, nor what it is assigned after it is
read, unless a loop around the read, or a jump, runs it again), also the last of
a comma, ``delete (m_bActive = false, m_pData)``; a function object made from it and called, like
``CDelete{m_pData}()``, does what its call operator does, and an operator
that isn't const but only checks it, like ``operator!``, doesn't release, nor
does a function that can be looked into that only checks a pointer it is
given, ``fg_CheckPointer(m_pData)``, also by giving it on as a pointer to
const, or giving a copy of a value it reads through it, ``fg_IsPositive(
*_pValue)``; a lambda that a function looked into calls does what its body does,
like ``[_pData] { delete _pData; }()`` of ``fg_Release(m_pData)``, but not one
it only makes, nor a branch of an ``if constexpr`` that is discarded, nor
what isn't evaluated, like ``noexcept(fg_Delete(m_pData))``; a function looked
into releases also through a local it assigns what it is given later, but not
by deleting what it allocates, ``new int(*_pData)``, nor by changing a local
copy of the pointer, ``pLocal = nullptr``; what a function that
returns what it is given gives, ``delete fg_Get(m_pData)``, also through a
local it makes of it or a cast, ``static_cast<int *>(_pData)``, and a
constructor that releases what it is given, ``CDelete Delete(m_pData)``, also either of
two, ``return _pFirst ? _pFirst : _pSecond;``, is of the object, but not what a
member function that can be looked into returns that isn't, like a scratch
copy, ``return new int(*m_pValue);``, also through any number of member
functions that return what the next returns, each worked out once, so that
what is told of one is the same wherever it is asked;
a local
pointed at something else on every path before, like ``pCopy = new
int(*pCopy)``, also by each branch of an ``if``, isn't of the object; a local assigned a member later, ``pData =
m_pData``, is of the object, and so is every capture of a lambda it calls; changing what
a pointer of it points to, like a count it shares, ``--*m_pCount``, also
of a pointer in a member, ``--m_Data.m_pState->m_nUsers``, also a member of
a member of what it points to, ``--m_pState->m_References.m_nUsers``, or read into a
local, also the address of one, ``auto pCount = &m_pShared->m_nUsers;
--*pCount;``, also through a reference member, ``--m_nUsers``, or what a getter of
a member, or its ``operator*``, gives a reference to through what that points
to, ``--m_Count.f_Get()`` of a reference wrapper or ``--*m_pCount`` of a
``TCPointer``, also atomically or by a helper
given its address, ``__atomic_fetch_sub(&m_nUsers, 1, __ATOMIC_RELAXED)``,
also through the object dereferenced, ``--(*m_pState).m_nUsers``, or a reference a
helper whose body can be seen gives, which isn't in the object, owns it too; where
a write lands is what the places it can be there are, by the flow of the function
it is in, so writing the object's own storage doesn't, however it is reached: through a
pointer to a member of the object itself, ``*ppData = nullptr`` or
``ppData[0] = nullptr`` with ``auto ppData = &m_pData``, a local reference to
the object, ``auto &Self = *this; Self.m_pData = nullptr;``, a cast of ``this``,
a byte of a number it holds, ``reinterpret_cast<uint8 *>(&m_Value)[4] = 0``,
or a helper that gives back the object it is given; an element of a pointer
member, ``m_pState[0]``, also through a local reference, ``auto &Count =
*m_pCount; --Count;``, and what an element of an array of pointers points
to, ``--*m_pCounts[0]``, are what the copies share),
or
one of its bases or members
releases something, also a closure that captures one. A view or an iterator,
which is at what it doesn't own, is not, even when copying it does more than
copy its bytes or it has an empty destructor, or one whose work is in a
branch of an ``if constexpr`` that its specialization discards or after a
statement that always leaves there, ``if constexpr (!t_bOwns) return; delete[]
m_pData;``, also in a function it calls, or one whose
destructor is defined in another file and that is copied as is, by an implicit
copy constructor, which an owner can't be; one that holds what it owns, like a
shared pointer, is. The body of a destructor of a template that isn't
instantiated isn't looked into: its instantiation tells what it does, but what
its members and bases release still makes it own. So a specialization whose
destructor nothing in the translation unit instantiates, like
``TCOwner<int>`` of a parameter that nothing calls the function with, isn't
known to own what its destructor would release, until a use instantiates it. When the function moves from the parameter, the diagnostic suggests
``&&``.

Coroutines are not checked, since they take what they need by value so it
survives suspension, nor functions that return a type a coroutine can return,
like ``TCFuture``, or whose specialization of ``std::coroutine_traits``,
for their result and parameters (and the object of a member function,
qualified with ``&&`` an rvalue reference), the translation unit has, or a
partial specialization for any parameters of the result or of results of its
template, ``coroutine_traits<TCFuture<t_CType>, tp_CParams...>``, or for the
parameters it names first and any after them, ``coroutine_traits<CTask, COwner,
tp_CParams...>`` (then without constraints, which aren't told, and with a
complete promise type, unless a specialization for exactly what the function
is given is defined, or declared explicitly, which is what it selects; the constraints of one for any parameters, like those
of Malterlib's futures, which reject only reference parameters of the member
functions of actors, are taken to accept them), with a
``promise_type`` that is a type, which can be made
coroutines without changing their signatures, like the member functions of actors (classes derived from
``NMib::NConcurrency::CActor``, not another class of that name), whose calls move the
arguments into them. A template is checked as each of its instantiations makes
it, by the types it has there, also a generic lambda; one that isn't
instantiated isn't checked. An explicit specialization is checked like the
instantiation it stands for, though what is changed to take the parameter by
reference can be the primary template (the check gives no fixes). Only
definitions are checked, since a declaration does
not tell whether the function is a coroutine, and pure virtual functions,
which are not coroutines but give their overrides their signatures.
Overrides and functions with C linkage are not checked either.

Nothing is reported in a translation unit with an error that stops it from
compiling: its control flow isn't built, and what the errors leave can't be
relied on.

.. example-check

  struct COwner
  {
    COwner();
    COwner(COwner const &_Other);
    ~COwner() { delete m_pData; }
    int *m_pData = nullptr;
  };
  struct CView
  {
    int const *m_pData;
  };
  struct CHolder
  {
    COwner m_Owner;
  };
  struct CEmptyDestructor
  {
    ~CEmptyDestructor() {}
    int *m_pData;
  };
  void fg_Use(COwner const &_Owner);
  void fg_UseView(CView const &_View);
  int fg_Borrow(COwner _Owner) // warning: an owner taken by value
  {
    return *_Owner.m_pData;
  }
  void fg_Unused(COwner _Owner) // warning: also one that nothing uses
  {
  }
  int fg_Member(CHolder _Holder) // warning: a class whose member releases something
  {
    return *_Holder.m_Owner.m_pData;
  }
  int fg_View(CView _View) // fine: a view, which is at what it doesn't own
  {
    return *_View.m_pData;
  }
  int fg_Empty(CEmptyDestructor _Value) // fine: an empty destructor
  {
    return *_Value.m_pData;
  }
  TCFuture<int> fg_Coroutine(COwner _Owner) // fine: a coroutine
  {
    co_return *_Owner.m_pData;
  }
  struct CBase
  {
    virtual void f_Take(COwner _Owner) = 0; // warning: pure virtual, which gives its overrides their signature
  };
  struct CDerived : CBase
  {
    void f_Take(COwner _Owner) override // fine: an override
    {
      fg_Use(_Owner);
    }
  };
  extern "C" void fg_CTake(COwner _Owner) // fine: C linkage
  {
    fg_Use(_Owner);
  }
  template <typename tf_CType>
  void fg_Template(tf_CType _Value) // fine: a template that isn't instantiated
  {
  }

Options
-------

.. option:: AllowedTypes

   Semicolon-separated list of types that can be taken by value, also through
   an alias of them, like a typedef. Qualified names match qualified names.
   Default is empty.

.. option:: MoveFunctions

   Semicolon-separated list of functions that move from their argument.
   Default is the functions that move and forward of the vocabulary, `MoveFunctions` and `ForwardFunctions` of the Malterlib options file (:doc:`/clang-tidy/MalterlibOptionsFile`): `move` and `forward` of the standard library, and `fg_Move` and `fg_Forward` of Malterlib, by unqualified name. The option edits the functions that move, and the functions that forward are taken with them.
