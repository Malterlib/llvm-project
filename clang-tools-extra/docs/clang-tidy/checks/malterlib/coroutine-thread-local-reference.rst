.. title:: clang-tidy - malterlib-coroutine-thread-local-reference

malterlib-coroutine-thread-local-reference
==========================================

Finds locals, parameters and lambda captures of a coroutine that refer to the
thread-local data of the thread that runs it and are used after a suspension.
A coroutine can resume on another thread, where what they refer to is the
data of the thread that ran before the suspension, which that thread uses, or
which is gone with it.

.. code-block:: c++

  TCFuture<void> f_Process()
  {
    auto &ThreadLocal = fg_ConcurrencyThreadLocal();
    co_await f_Other();
    ThreadLocal.f_Something();  // warning: may be another thread's
  }

The analysis is local to the function: thread-local data can only be referred
to from locals and parameters, which
:doc:`malterlib-thread-local-storage <thread-local-storage>` makes sure of,
so nothing else in the program holds a pointer or a reference to it.

Thread-local data in a function is:

- what a call of one of :option:`ThreadLocalFunctions` gives, called with
  nothing but its object, like ``fg_ConcurrencyThreadLocal()``,
  ``g_ThreadLocal.f_Get()`` or ``g_ThreadLocal->m_Value`` of a
  ``TCThreadLocal``, also one with an explicit object parameter, not that of
  a thread named, ``fg_SystemThreadLocal(_ThreadID)``, nor what that gives
  of a thread-local type;
- a variable declared ``thread_local``;
- what a parameter or a local of a reference or a pointer to a thread-local
  type refers to, one of :option:`ThreadLocalTypes`, and what ``this``
  points to in a member function of one;
- what a call gives whose return type is a reference or a pointer to a
  thread-local type, and what a member function of it gives by reference or
  pointer, ``int &Value = Data.f_Value()``, unless its body gives what a
  pointer it holds points to, or a copy;
- the members and the elements of those, and the local references, pointers
  and arrays of pointers bound to them or assigned them, where they are, also
  through a local reference or a local pointer to a pointer, ``int *&rpValue =
  pValue``, assignments as values, pointers stepped or moved, and the names of
  a structured binding of them, ``auto &[Value] = t_Data``, as one classifier
  of every kind of expression tells what an expression is made of; not what a
  branch a constant doesn't take gives.

A local, a parameter or a capture that may refer to it, on some path, where a
suspension is, is reported where it is used after the suspension, the first
use of each: read, stepped, ``++pValue``, moved or given by reference, also
through a local reference or a local pointer to it, ``**ppValue``; one given it again after the suspension, ``pData =
&fg_ConcurrencyThreadLocal()``, is of the thread then; one copied from another is what that one is where it is
copied. An assignment through a reference changes what it refers to, not
which. ``this`` of a member function of a thread-local type, also that a
lambda in one captures, is reported where it is used after a suspension, by
a member. What a parameter or a
capture refers to is of the thread that called the coroutine or made the
lambda, and of a coroutine whose promise suspends before its body runs, like
a generator, it is stale from the start. What is gotten in an expression
before a suspension in it, ``(&t_Value, co_await f_Other(), ...)``, is of the
thread before; the left side of an assignment is evaluated after its right
side. A handler that catches what a suspension throws comes after it; what is
assigned before its ``try`` doesn't reach it. A loop over it goes on over the
data of the thread before after a suspension in it that it goes on after, also
one whose iterators are classes, not one that returns or breaks after it. Only
suspensions that can be reached count.

A copy of it is the coroutine's own, ``auto Copy = fg_ConcurrencyThreadLocal()``,
and so is a copy of what a reference refers to that a lambda captures,
``[Data]``.

A local of one of :option:`ThreadLocalScopeTypes`, like ``CCurrentActorScope``,
which keeps a reference to it, is reported when it is alive across a
suspension that can be reached, also one in an initializer,
``int Value = co_await f_Other()``; it is alive from its declaration, also in
what a statement starts with, ``if (CCurrentActorScope Scope; ...)``, or as a
temporary a reference extends, to the end of its scope; a parameter held by
value from the start; a temporary to the end of its full expression,
``(CCurrentActorScope(...), co_await f_Other())``. One that derives from one of :option:`ThreadLocalHandlerTypes`,
like Malterlib's ``CCrossActorCallStateScope`` of
``CCoroutineThreadLocalHandler``, isn't: the coroutine runtime calls its
``f_Suspend`` and ``f_Resume`` around every suspension, on the thread it
resumes on.

Only coroutines are checked, not a callback that runs on another thread.

Options
-------

.. option:: ThreadLocalFunctions

   Semicolon-separated list of the functions that give the thread-local data
   of the thread that calls them, also a part of it, by name, also after the
   names of the classes and namespaces they are in, `TCThreadLocal::f_Get`,
   an operator by its name, `TCThreadLocal::operator->`, and a conversion by
   the type its class declares, also qualified, `TCThreadLocal::operator
   t_CData *` or `CProvider::operator NData::CData *`. None is built in;
   Malterlib's options file has Malterlib's, like `fg_ConcurrencyThreadLocal`,
   the members of `TCThreadLocal` and `TCThreadLocalDynamic` that give the
   data, and `CAsyncResult::fs_ResultWasNotSetException`.

.. option:: ThreadLocalTypes

   Semicolon-separated list of the classes of thread-local data, by name, or
   by the names of the namespaces and classes they are in, the anonymous and
   inline namespaces aside, up to a function a class is local to,
   `NMib::NFile::CThreadLocal`, which tells apart classes of the same name.
   Only these are thread-local types, in every translation unit alike; each
   class one of :option:`ThreadLocalTemplates` holds must be one
   (:doc:`malterlib-thread-local-storage <thread-local-storage>`). None is
   built in; Malterlib's options file has Malterlib's, like
   `CConcurrencyThreadLocal`, `CSystemThreadLocal` and
   `CPromiseThreadLocal`.

.. option:: ThreadLocalTemplates

   Semicolon-separated list of the class templates that hold a value for each
   thread, by name, whose first argument, a class that isn't a template's,
   must be one of :option:`ThreadLocalTypes`. None is built in; Malterlib's
   options file has `TCThreadLocal` and `TCThreadLocalDynamic`.

.. option:: ThreadLocalScopeTypes

   Semicolon-separated list of the scopes that keep a reference to
   thread-local data and that the coroutine runtime doesn't move with the
   coroutine. None is built in; Malterlib's options file has
   `CCurrentActorScope` and `CCurrentlyProcessingActorScope`.

.. option:: ThreadLocalHandlerTypes

   Semicolon-separated list of the bases of the scopes that the coroutine
   runtime suspends and resumes with the coroutine, on the thread it resumes
   on. None is built in; Malterlib's options file has
   `CCoroutineThreadLocalHandler`.

Each is also an option of all checks in the options file
(:doc:`../../MalterlibOptionsFile`), which
:doc:`malterlib-thread-local-storage <thread-local-storage>` reads too.
