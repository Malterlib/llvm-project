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
a pointer to one, and the local aliases, members and elements of those.

Reported is a pointer or a reference to it stored in:

- a member, by an assignment, a member initializer, a default member
  initializer, also of a template's instantiation, a constructor's body or an
  aggregate initialization, also of a local and of its arrays, ``CView
  View{&t_Value}`` or ``CView View{{&t_Value}}``;
- a member by a constructor given it, ``CView View(&t_Value)``, which is
  reported where the object is made, unless it ends with the expression or
  is a local of a function that isn't a coroutine: of a constructor that can
  be seen, one that stores the parameter it is given, or what it points or
  refers to; of one that can't, a parameter of a pointer or a reference to
  what isn't ``const``;
- a member, a global or an object on the heap by a function given it whose
  body stores it there where that can run, also through a local alias of the
  member, also by giving it to another that does, of an object that stays, or
  a local one in a coroutine, ``Holder.f_Set(&t_Value)``, or in what it is
  given for another parameter that stays, not a local of its own, like a
  formatter, nor a temporary, also with an
  explicit object parameter, reported where it is given, when the function
  can't tell it is thread-local data: a parameter of another type, ``int *``,
  also by reference, ``int *const &``, or of a type of a template's argument,
  ``t_CType *``; not a member the exemptions below cover. A template's instantiation isn't
  reported for what is thread-local data only as its arguments are, so a
  ``NOLINT`` where the data is given tells why;
- a global or a static variable, also in a member or a capture of its
  initializer;
- an object on the heap, ``new int *(&t_Value)`` or ``new int *[1]{&t_Value}``;
- what an output parameter refers to, ``o_pData = &fg_ConcurrencyThreadLocal()``;
- a lambda capture: a reference to it or a pointer into it captured by
  reference, also a name of a structured binding or a reference to a local
  pointer, a pointer into it or an array of them copied, ``[pData]``, also
  through a reference to a pointer, ``[rpData]``, an initializer,
  ``[pData = &t_Data]``, or ``this`` of a member function of a thread-local
  type, ``[this]``; not a copy of what a reference refers to, ``[Data]``, or
  of the object, ``[*this]``;
- a return, unless the return type is a reference or a pointer to a
  thread-local type, by which callers recognize it, or the function is a
  member function of a thread-local type, what its callers tell is in it.

A local, a parameter held by value, or an element of a local array can hold
it; a local object only outside a coroutine and when it doesn't leave the
function, returned or stored; a value copied out of it can be stored anywhere. So can a closure that
ends with the function before it suspends: one called where it is made,
``[&] { ... }()``, a local only called, or the cleanup of a local guard of
one of :option:`ScopeExitHelpers` that is only run or cancelled, ``auto
Cleanup = g_OnScopeExit / [&] { ... };``, unless a suspension that can run
comes before its end, also in its captures. What can't run, in a branch a
constant doesn't take or after a return, also by the control flow of a
lambda, isn't reported. A generic lambda is checked as instantiated. A function of
:option:`ThreadLocalFunctions` may return it by any type, and the member
functions of :option:`ThreadLocalTemplates` aren't checked.

A variable or a member that holds one of :option:`ThreadLocalTemplates` of a
class for each thread, also as an argument of another template,
``TCAggregate<TCThreadLocal<CData>>``, each of them, also in an argument pack
or a member of a template's instantiation, and an object made on the heap
that holds one, is reported when the class isn't one
of :option:`ThreadLocalTypes`: only those are taken as thread-local data, in
every translation unit alike, also one that sees the class through a function
that gives it but not the template. Add it to :option:`ThreadLocalTypes` in
the options file, or hold a struct of its own for the thread instead, also of
a class that is used as ordinary data too. A template's class, like
``TCStr<...>``, and a type that isn't a class aren't reported; what one of
:option:`ThreadLocalFunctions` gives of them is still thread-local data.

Not reported is what is thread-local data itself, of the same thread, like
what a ``TCThreadLocal`` of a pointer holds, nor a member of:

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
   makes a guard that runs the closure it is given where the guard ends.
   None is built in; Malterlib's options file has ``g_OnScopeExit``,
   ``g_OnScopeExitWithException`` and ``g_OnScopeExitCatch``.

Each is also an option of all checks in the options file
(:doc:`../../MalterlibOptionsFile`).
