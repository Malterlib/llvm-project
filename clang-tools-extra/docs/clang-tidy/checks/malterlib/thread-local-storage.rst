.. title:: clang-tidy - malterlib-thread-local-storage

malterlib-thread-local-storage
==============================

Finds pointers and references to thread-local data stored where they can
outlive the expression that stores them, in any function, not only a
coroutine. Thread-local data is only to be referred to from locals and
parameters, and gotten again where it is used: what else holds it can be
used on another thread, after the thread is gone, or by a coroutine that
resumes on another thread, which
:doc:`malterlib-coroutine-thread-local-reference
<coroutine-thread-local-reference>` then can't see.

.. example-options malterlib-thread-local-storage.ThreadLocalTypes: 'CConcurrencyThreadLocal'

.. example-before
   struct CConcurrencyThreadLocal
   {
   };

.. code-block:: c++

  struct CView
  {
    CView(CConcurrencyThreadLocal &_ThreadLocal)
      : m_pThreadLocal(&_ThreadLocal)  // warning
    {
    }
    CConcurrencyThreadLocal *m_pThreadLocal;
  };

What is thread-local data is told within the function as by
:doc:`malterlib-coroutine-thread-local-reference
<coroutine-thread-local-reference>`: what :option:`ThreadLocalFunctions`
give, ``thread_local`` variables, what a parameter or a local of a reference or
a pointer to one of the thread-local types refers to, also ``this`` of a
member function of one, what a call gives whose return type is a reference or
a pointer to one, or whose body gives back thread-local data of its own, like
a getter of a ``thread_local`` variable, ``int &f_Counter() { static
thread_local int s_Value; return s_Value; }``, not one whose returns each
give what a thread named gives, ``return fg_SystemThreadLocal(_ThreadID);``,
a return of nothing, ``return nullptr;``, giving no way, and the local aliases, members and elements of those.

Reported is a pointer or a reference to it stored in:

- a member, by an assignment, a member initializer, a default member
  initializer, also of a template's instantiation, a constructor's body or an
  aggregate initialization, also of a local and of its arrays, ``CView
  View{&t_Value}`` or ``CView View{{&t_Value}}``;
- a base or a member by a constructor given it, ``CView View(&t_Value)``,
  ``CDerived(...) : CBase(&_Data.m_Value)``, also one a derived class
  inherits, ``using CBase::CBase;``, which is
  reported where the object is made, unless it ends with the expression or
  is a local of a function that isn't a coroutine: of a constructor that can
  be seen, one that stores the parameter it is given, or what it points or
  refers to, each at the level it is kept, what a parameter refers to, or
  the value loaded through it; of one that can't, a parameter of a pointer or
  a reference, also to what is ``const``, of a class that can hold a pointer
  or a reference to it, not a void pointer or bytes;
- a member, a global or an object on the heap by a function given it whose
  body stores it there where that can run, also through a local alias of the
  member, also by giving it to another that does, of an object that stays, or
  a local one in a coroutine, ``Holder.f_Set(&t_Value)``, also what a
  pointer to it points to, ``pHolder->f_Set(&t_Value)``, not a temporary that
  a member function gives back of itself, ``"{}"_f << Value``, or in what it is
  given for another parameter that stays, not a local of its own, like a
  formatter, nor a temporary, also with an
  explicit object parameter, reported where it is given, when the function
  can't tell it is thread-local data: a parameter of another type, ``int *``,
  also by reference, ``int *const &``, or of a type of a template's argument,
  ``t_CType *``; not a member the exemptions below cover. A template's instantiation isn't
  reported for what is thread-local data only as its arguments are, so a
  ``NOLINT`` where the data is given tells why; what it returns or captures
  of that, also in a lambda in it that isn't generic, ``[_pData] { return
  &_pData->m_Link; }()``, is reported where code that isn't a template's made
  the instantiation, ``m_ThreadLocals.f_Insert(this)`` of a generic list whose
  link from a member is returned as a pointer to the link, but is for what is
  thread-local data by itself, also through a local alias; a function given a
  reference or a pointer to a thread-local type reports its own stores where
  it is defined, also in another translation unit, so its callers don't; what
  a constructor delegated to keeps in the object is the object's own, judged
  where the object is made;
- a global or a static variable, also in a member or a capture of its
  initializer, in a temporary it extends, ``const CView &View = CView{...}``,
  or in the array of a ``std::initializer_list`` it holds, and in what is
  copied or assigned from a temporary, ``g_View = CView{&t_Value}``;
- an object on the heap, ``new int *(&t_Value)`` or ``new int *[1]{&t_Value}``;
- the frame of a coroutine whose body can be seen given it as an argument,
  which keeps its parameters across its suspensions, ``co_await
  fg_ReadLater(&t_Value)``; not of one that can't suspend, a ``TCFuture``
  coroutine whose body never suspends and that an override can't replace,
  which runs to its end in the call, ``co_await fg_ReadNow(&t_Value)`` of
  ``TCFuture<int> fg_ReadNow(int const *_pValue) { co_return *_pValue; }``,
  nor of one called on it, ``co_await
  fg_ConcurrencyThreadLocal().m_Statistics.f_Read()`` of ``TCFuture<int>
  f_Read() const { co_return m_Count; }``;
- what an output parameter refers to, ``o_pData = &fg_ConcurrencyThreadLocal()``;
- a lambda capture: a reference to it or a pointer into it captured by
  reference, also a name of a structured binding or a reference to a local
  pointer, a pointer into it or an array of them copied, ``[pData]``, also
  through a reference to a pointer, ``[rpData]``, an initializer,
  ``[pData = &t_Data]``, or ``this`` of a member function of a thread-local
  type, ``[this]``; a local captured by reference, also by an init capture of
  a reference to it, ``[&pCurrent = pText]``, by what it holds where the
  lambda is made and after; not a copy of what a reference refers to,
  ``[Data]``, or of the object, ``[*this]``, nor a capture by reference of a
  copy the lambda around it keeps, ``[Stats] { [&Stats] { ... }; }``, unless
  that copy is a pointer into it;
- a return, unless the return type is a reference or a pointer to a
  thread-local type, by which callers recognize it, or the function is a
  member function of a thread-local type, what its callers tell is in it.

A local, a parameter held by value, or an element of a local array can hold
it; a local object, of what its constructor or a member function keeps in
it, only outside a coroutine and when what it holds doesn't leave the
function: a member function called on it, or a member of it read as a number
or read through, also in a closure that stays local, is its own use, unless
what it gives can hold a reference or a pointer, by its type, and leaves; a
copy of a part of it that can't hold one either, a value of its own, ``return
Report.m_Stats;``, carries nothing it holds; a
copy, a return, a reference to it or a pointer of it given on, anywhere,
makes it leave, also ``return *View.m_pValue`` of a function that returns a
reference, a getter's result returned, or an array of them captured by a
closure that leaves; a value copied out of it can be stored anywhere; what
isn't evaluated, ``sizeof(View)``, isn't a use. So can a closure that
stays local, which ends with the function before it suspends, as one
classifier of each use of it tells, the same that tells
:doc:`malterlib-coroutine-thread-local-reference
<coroutine-thread-local-reference>` where a closure runs. Each use is a call,
by its call operator, also named or with it as the explicit object, by
``std::invoke``, or of what only gives it back, ``fg_Move(fRead)()``; an
inspection, a conversion to ``bool``, a presence check, ``if (fRead)`` or
``fRead.f_IsEmpty()``, a comparison, or ``sizeof`` or ``decltype`` of it; an
alias, a local reference to it, a local it is copied to as it is made, a
temporary, or a local or temporary function object of
``SynchronousFunctionTypes`` of the options file
(:doc:`../../MalterlibOptionsFile`) made of it or assigned it,
``TCFunction<bool ()> fRead = [&] { ... };`` or ``fRead = [&] { ... };``,
whose uses are classified the same way; a local one is a slot of closure
values, which holds the closure from where it is made with it or assigned it
to where it is assigned another callable, ``nullptr`` or another function
object, or is cleared, by a member function of it that can change it, has no
parameters and gives nothing, ``fRead.f_Clear()``, on the ways it reaches, and
only the uses it reaches are the closure's: a later callable given away,
``fRead = [] { return 0; }; fg_Save(fRead);``, isn't; a local reference bound
to it, ``auto &fAlias = fRead;``, is a second name of that slot, through
which it is called, given away, assigned or cleared as through its own; a
giving to a
parameter of a function that only calls it, which its body tells by the same
classifier, also through a temporary function object of such a type,
``fg_Run([&] { ... })`` of ``void fg_Run(TCFunction<void ()> const &)``,
``fg_ForEach(Items, [&](auto &) { ... })``; a capture by another closure that
stays local itself, whose uses of the capture in its body are classified the
same way, ``auto fBatch = [&] { fg_ForEach(Items, fVisit); };``, or by itself, through
the function object it is in, to call itself, ``fVisit = [&](int _Depth) {
if (_Depth) fVisit(_Depth - 1); };``, whose uses in its body are its own; or an
escape, anything else, also a copy of the closure, ``auto fCopy = fRead``. A closure
stays local when no use, also of an alias or in a capturing closure, escapes,
or when it is only given to make a local guard of one of
:option:`ScopeExitHelpers` that is only run or cancelled, ``auto Cleanup =
g_OnScopeExit / [&] { ... };`` or ``auto Cleanup = fg_OnScopeExit([&] { ...
});``; in a coroutine, not when a suspension that can
run is in its captures or in what calls it, or comes while a local it is in,
or the guard, is alive; of a local function object of
``SynchronousFunctionTypes``, while it holds the closure, not before it is
assigned it, ``TCFunction<int ()> fRead; co_await fg_Work(); fRead = [&Data]
{ ... };``, nor after it is assigned another or cleared, unless a copy of it
holds it then. What can't run, in a branch a
constant doesn't take or after a return, also by the control flow of a
lambda, isn't reported, also in a member initializer. The storage and what
a pointer stored in it points to are told apart, as the coroutine check tells
them: what a local array or aggregate holds is what it is made with, and a
function given a reference to a pointer that it reads stores the pointer's
value, not the storage it refers to. What a default argument or a default member
initializer gives, also what it evaluates, is checked where it is used and
reported there, of a default constructor the compiler defines where what it
makes is constructed, once for each place, and a call through an unchanged local pointer to a function
or a member function as a call of that function. A pointer stepped, ``++pValue`` or ``pValue += 1``, points
where it did. A generic lambda is checked as instantiated. A function of
:option:`ThreadLocalFunctions` may return it by any type, and the member
functions of :option:`ThreadLocalTemplates` aren't checked.

A variable or a member that holds one of :option:`ThreadLocalTemplates` of a
class for each thread, also as an argument of another template,
``TCAggregate<TCThreadLocal<CData>>``, each of them, also in an argument pack
or a member of a template's instantiation, and an object made on the heap
that holds one, is reported when the class isn't one, in an instantiation of a
template where code that isn't a template's made it, a factory's call,
of :option:`ThreadLocalTypes`: only those are taken as thread-local data, in
every translation unit alike, also one that sees the class through a function
that gives it but not the template. Add it to :option:`ThreadLocalTypes` in
the options file, or hold a struct of its own for the thread instead, also of
a class that is used as ordinary data too. A template's class, like
``TCStr<...>``, and a type that isn't a class aren't reported; what one of
:option:`ThreadLocalFunctions` gives of them is still thread-local data.

Nothing is reported in a translation unit with an error that stops it from
compiling: its control flow isn't built, and what the errors leave can't be
relied on.

.. example-options malterlib-thread-local-storage.ThreadLocalTypes: 'CConcurrencyThreadLocal'

.. example-check

  struct CConcurrencyThreadLocal
  {
    int m_Value;
    int *m_pValue;
  };
  CConcurrencyThreadLocal &fg_ConcurrencyThreadLocal();
  thread_local int t_Value = 0;
  thread_local int *t_pValue = nullptr;
  struct CView
  {
    int *m_pValue;
  };
  struct CHolder
  {
    void f_Set(int *_pValue) { m_pValue = _pValue; }
    int *m_pValue = nullptr;
  };
  CView g_View;
  CHolder g_Holder;
  int *g_pValue = nullptr;
  void fg_Member()
  {
    g_View.m_pValue = &t_Value; // warning: stored in a member of a global
  }
  void fg_Global()
  {
    g_pValue = &fg_ConcurrencyThreadLocal().m_Value; // warning: stored in a global
  }
  void fg_Setter()
  {
    g_Holder.f_Set(&t_Value); // warning: stored by a function whose body stores it
  }
  void fg_Aggregate()
  {
    CView View{&t_Value}; // warning: a member of a local by aggregate initialization
    (void)View;
  }
  void fg_Local()
  {
    int *pValue = &t_Value; // fine: a local
    (void)pValue;
  }
  void fg_ThreadLocalItself()
  {
    t_pValue = &t_Value; // fine: what is thread-local data itself, of the same thread
  }
  void fg_NotRun()
  {
    if (false) // fine: what can't run
      g_pValue = &t_Value;
  }

Not reported is what is thread-local data itself, of the same thread, like
what a ``TCThreadLocal`` of a pointer holds or a ``thread_local`` variable,
also an object an accessor gives, ``g_View.f_Get()->f_Set(&t_Value)``, not
what a pointer it holds points to, ``t_pHolder->f_Set(&t_Value)``, and the
own storage of the classes below, also a member of a member of one or of a
base of it, not what a pointer it holds points to,
also as it is initialized, through a local reference to it, a setter called on
it, an object assigned to it, or the capture of a closure held in it, nor a
member of the following, what their constructors keep in the object (what
they store elsewhere still is):

- a thread-local type itself, like ``CConcurrencyThreadLocal::m_SystemThreadLocal``;
- a class that derives from one of :option:`ThreadLocalHandlerTypes`, like
  ``CCoroutineThreadLocalHandler``, which the coroutine runtime suspends and
  resumes with the coroutine, on the thread it resumes on;
- one of :option:`ThreadLocalScopeTypes`, a scope that keeps a reference to it
  and that :doc:`malterlib-coroutine-thread-local-reference
  <coroutine-thread-local-reference>` reports alive across a suspension.

Options
-------

.. option:: ThreadLocalFunctions

   As of :doc:`malterlib-coroutine-thread-local-reference
   <coroutine-thread-local-reference>`. A function of it may return the data
   by any type.

.. option:: ThreadLocalTypes

   As of :doc:`malterlib-coroutine-thread-local-reference
   <coroutine-thread-local-reference>`.

.. option:: ThreadLocalTemplates

   As of :doc:`malterlib-coroutine-thread-local-reference
   <coroutine-thread-local-reference>`. Their member functions, which are
   their implementation, aren't checked.

.. option:: ThreadLocalScopeTypes

   As of :doc:`malterlib-coroutine-thread-local-reference
   <coroutine-thread-local-reference>`.

.. option:: ThreadLocalHandlerTypes

   As of :doc:`malterlib-coroutine-thread-local-reference
   <coroutine-thread-local-reference>`.

.. option:: ScopeExitHelpers

   Semicolon-separated list of the variables, by name, whose ``operator /``
   makes a guard that runs the closure it is given where the guard ends,
   ``g_OnScopeExit / [&] { ... }``, and of the functions that make one of the
   closure they are given first, ``fg_OnScopeExit([&] { ... })``. None is
   built in; Malterlib's options file has ``g_OnScopeExit``,
   ``g_OnScopeExitWithException``, ``g_OnScopeExitCatch``,
   ``fg_OnScopeExit`` and ``fg_OnScopeExitCatch``.

Each is also an option of all checks in the options file
(:doc:`../../MalterlibOptionsFile`).
