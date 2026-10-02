.. title:: clang-tidy - malterlib-coroutine-thread-local-reference

malterlib-coroutine-thread-local-reference
==========================================

Finds locals, parameters and lambda captures of a coroutine that refer to the
thread-local data of the thread that runs it and are used after a suspension.
A coroutine can resume on another thread, where what they refer to is the
data of the thread that ran before the suspension, which that thread uses, or
which is gone with it.

.. example-options malterlib-coroutine-thread-local-reference.ThreadLocalTypes: 'CConcurrencyThreadLocal'

.. example-before
   struct CConcurrencyThreadLocal
   {
     void f_Something();
   };
   CConcurrencyThreadLocal &fg_ConcurrencyThreadLocal();
   struct CExample
   {
     TCFuture<void> f_Other();

.. example-after
   };

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
  points to in a member function of one, not in a lambda that copies the
  object, ``[*this]``, nor in the captures of a lambda in one, ``[*this] {
  [pValue = &m_Value] { ... }; }``;
- what a call gives whose return type is a reference or a pointer to a
  thread-local type, and what a member function of it gives by reference or
  pointer, ``int &Value = Data.f_Value()``, unless its body gives what a
  pointer it holds points to, or a copy, or each of its returns gives what a
  thread named gives, ``return fg_SystemThreadLocal(_ThreadID);``, also
  through another such function, a return of nothing, ``return nullptr;``,
  giving no way;
- the members and the elements of those, and the local references, pointers
  and arrays of pointers bound to them or assigned them, where they are, also
  through a local reference or a local pointer to a pointer, ``int *&rpValue =
  pValue``, assignments as values, pointers stepped or moved, and the names of
  a structured binding of them, ``auto &[Value] = t_Data``, as one classifier
  of every kind of expression tells what an expression is made of; not what a
  branch a constant doesn't take gives.

A local, a parameter or a capture that may refer to it, on some path, where a
suspension is, is reported where it is used after the suspension, the first
use of each: read, stepped, ``++pValue`` or ``pValue += 1``, also when what
it gives is dropped, moved, given by reference or its address given on,
``fg_Read(&Value)``, not the address of a local pointer, ``&pValue``, which
doesn't read it, also what is reached through it after
a suspension in the same expression, ``pValue[co_await f_Index()]`` or
``Data.f_Add(co_await f_Other())``, also through a local reference or a
local pointer to it, ``**ppValue``; not a pointer refreshed through a local
reference to it, ``rpValue = &t_Value``. A reference to a pointer that is
the data itself, ``int *&rpSlot = t_Data.m_pValue``, refers to it. A copy a
lambda captures is what it is given where the lambda is made; one it
captures by reference, also an init capture of a reference to a local,
``[&pCurrent = pText]``, is what the local holds there and after. one given it again after the suspension, ``pData =
&fg_ConcurrencyThreadLocal()``, is of the thread then; one copied from another is what that one is where it is
copied. An assignment through a reference changes what it refers to, not
which. ``this`` of a member function of a thread-local type, also that a
lambda in one captures, is reported where it is used after a suspension, by
a member. What a parameter or a
capture refers to is of the thread that called the coroutine or made the
lambda, and of a coroutine whose body doesn't start where it is called, it is
stale from the start. How a coroutine starts is told by its type, never by
looking into its promise: the body of a ``TCFuture``, ``TCFutureWithFlags`` or
``TCUnsafeFuture`` starts in the call, whatever the promise's initial suspend
does at run time; of another coroutine type, one whose promise's
``initial_suspend`` returns ``std::suspend_always``, like a generator, doesn't,
one whose awaiter's ``await_ready()`` only returns a constant expression starts
in the call where that is true and doesn't where it is false, and any other
does. What is gotten in an expression
before a suspension in it, ``(&t_Value, co_await f_Other(), ...)``, is of the
thread before; the left side of an assignment is evaluated after its right
side. A handler that catches what a suspension throws comes after it: what a
variable holds before and after each part of its ``try`` reaches it, also what
it is given before the ``try``, stale after a suspension in it, unless it is
given another value before the ``try``. A loop over it goes on over the
data of the thread before after a suspension in it, also one whose iterators
are classes, whether or not it goes on after the suspension. Each value of a
conditional is followed on its own path. Only suspensions that can be reached
count. What a closure made in it assigns, ``[&] { pData =
&fg_ConcurrencyThreadLocal(); }``, is assigned where it can run, by the one
classifier of its uses that :doc:`malterlib-thread-local-storage
<thread-local-storage>` tells a closure that stays local by: where it, an
alias of it or a closure that captures it is called or given to what only
calls it, where a guard made of it ends, and, of one that escapes, at each
call after it is made; not where it is only inspected, ``if (fRefresh)``.
What it assigns on each way through it, also through a pointer it copies
whose value is the address of the variable, ``[ppData = &pData] { *ppData =
&fg_ConcurrencyThreadLocal(); }``, replaces what the variable held where
it or an alias of it is called, also through a function object of
``SynchronousFunctionTypes``, ``TCFunction<void ()> fRefresh = [&] { ... };
fRefresh();``, where it holds that closure only: a call after it is given
another callable or cleared doesn't run it, and one where it may hold
another, on another way there, may not, and
where a local closure that calls it on each way through its own body is
called, ``auto fRead = [&] { fRefresh(); }; fRead();``.

What gives the data and is used after a suspension later in its own
expression, as C++17 orders it, is reported where it is gotten: the object of
a member function called with what suspends, ``fg_Data().f_Add(co_await
f_Other())``, the pointer of a subscript before its index,
``t_Values[co_await f_Index()]``, and an argument before or after another
that suspends, ``fg_Read(co_await f_Other(), &t_Value)``, which can be
evaluated before it in either order, also a use in such an argument,
``fg_Add(*pValue, co_await f_Other())``, but not one of what the argument
itself gives a value first on each way to where it reads it, in an operand
evaluated before, a condition or the left of a comma, ``fg_Add((pData =
g_Data.f_Get()) ? pData->m_Value : 0, co_await f_Other())`` or ``fg_Add((pData
= &t_Data, pData->m_Value), co_await f_Other())``, which is on the same side
of the suspension; and of
the captures of a lambda,
whose order isn't told, also what a default argument gives, each use of it
apart,
which is evaluated where it is used; not the left side of an assignment,
which is evaluated after its right side, nor a value of a conditional, each
of which is followed on its own path. A value given to what either of
several pointers can be, ``*(bFirst ? &pFirst : &pSecond) = ...``, may be
given to each of them. A pointer read before
such a suspension is what it was then, also when the variable is given
another value later in the expression.

What is stored in thread-local storage points elsewhere: only the storage
itself is thread-local data, ``int *pValue = t_pData`` isn't, unless it is a
pointer to a thread-local type; what a local array of pointers or a local
aggregate holds is what it is made with, also read through ``std::bit_cast``.
A copy of it is the coroutine's own, ``auto Copy = fg_ConcurrencyThreadLocal()``,
and so is a copy of what a reference refers to that a lambda captures,
``[Data]``.

A local of one of :option:`ThreadLocalScopeTypes`, like ``CCurrentActorScope``,
which keeps a reference to it, is reported when it is alive across a
suspension that can be reached, also one in an initializer,
``int Value = co_await f_Other()``; it is alive from its declaration, also in
what a statement starts with, ``if (CCurrentActorScope Scope; ...)``, or as a
temporary a reference extends, to the end of its scope; a parameter held by
value, also across the initial suspension of a promise that suspends before
the body runs, and one a lambda captures, from the start; a temporary to the
end of its full expression, where it is destroyed, made in any way, also by
a default argument or in an argument that can be evaluated before another
that suspends, also as a member, an element or a capture of what a variable
is made of, from where it is made, also as an aggregate or by a
call, and one made in what is awaited, ``co_await (CCurrentActorScope(...),
f_Other())``, where it can be made, on its own path of a conditional; not one
on the left of an assignment whose right side suspends. A local that holds one
by value is one too: an array of them, an aggregate, or a closure that
captures one, ``auto fScope = [Scope = CCurrentActorScope(...)] {};``. One that derives from one of :option:`ThreadLocalHandlerTypes`,
also with one in it,
like Malterlib's ``CCrossActorCallStateScope`` of
``CCoroutineThreadLocalHandler``, isn't: the coroutine runtime calls its
``f_Suspend`` and ``f_Resume`` around every suspension, on the thread it
resumes on.

Only coroutines are checked, not a callback that runs on another thread.

Nothing is reported in a translation unit with an error that stops it from
compiling: its control flow isn't built, and what the errors leave can't be
relied on.

.. example-options malterlib-coroutine-thread-local-reference.ThreadLocalTypes: 'CConcurrencyThreadLocal;CSystemThreadLocal'

.. example-check

  struct CSystemThreadLocal
  {
    int m_Value;
  };
  struct CConcurrencyThreadLocal
  {
    int m_Value;
    int *m_pValue;
  };
  CConcurrencyThreadLocal &fg_ConcurrencyThreadLocal();
  CSystemThreadLocal &fg_SystemThreadLocal(int _ThreadID);
  thread_local int t_Value = 0;
  thread_local int *t_pData = nullptr;
  TCFuture<void> fg_Other();
  int fg_Read(int const *_pValue);
  TCFuture<void> fg_Stale()
  {
    auto &ThreadLocal = fg_ConcurrencyThreadLocal(); // warning: may be another thread's
    co_await fg_Other();
    ThreadLocal.m_Value = 1;
    co_return {};
  }
  TCFuture<void> fg_Refreshed()
  {
    auto *pData = &fg_ConcurrencyThreadLocal(); // fine: given again after the suspension
    co_await fg_Other();
    pData = &fg_ConcurrencyThreadLocal();
    pData->m_Value = 1;
    co_return {};
  }
  TCFuture<void> fg_Copy()
  {
    auto Copy = fg_ConcurrencyThreadLocal(); // fine: a copy is the coroutine's own
    co_await fg_Other();
    Copy.m_Value = 1;
    co_return {};
  }
  TCFuture<void> fg_ThreadNamed()
  {
    auto &Named = fg_SystemThreadLocal(1); // fine: the data of a thread named
    co_await fg_Other();
    Named.m_Value = 1;
    co_return {};
  }
  TCFuture<void> fg_ThreadLocalVariable()
  {
    int *pValue = &t_Value; // warning: a variable declared thread_local
    co_await fg_Other();
    *pValue = 1;
    co_return {};
  }
  TCFuture<void> fg_Stored()
  {
    int *pValue = t_pData; // fine: what is stored in thread-local storage points elsewhere
    co_await fg_Other();
    *pValue = 1;
    co_return {};
  }
  TCFuture<void> fg_SameExpression()
  {
    fg_Read((co_await fg_Other(), &t_Value)); // fine: gotten after the suspension in the expression
    co_return {};
  }
  TCFuture<void> fg_NotReached()
  {
    int *pValue = &t_Value; // fine: a suspension that can't be reached
    if (false)
      co_await fg_Other();
    *pValue = 1;
    co_return {};
  }

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
