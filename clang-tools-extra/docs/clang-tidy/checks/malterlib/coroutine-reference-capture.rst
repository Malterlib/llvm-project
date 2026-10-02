.. title:: clang-tidy - malterlib-coroutine-reference-capture

malterlib-coroutine-reference-capture
=====================================

Finds lambdas whose call operators are ``TCFuture`` coroutines and that
capture variables by reference where the variables can be gone when the
lambda uses them. A copy of a pointer into a local, like ``[pValue = &Value]``
or ``[pValue]`` of an unchanged ``int *pValue = &Value``, also through a
local reference to the pointer, ``[pAlias]`` of ``int *&pAlias = pValue``, also taken by
``std::addressof`` or ``__builtin_addressof``, also cast to another
pointer, ``static_cast<int const *>(&Value)``, also into a member
or an element of it, also of what a local pointer points to, ``&pValues[1]``,
also of a structured binding, ``auto [Value] = ...``, also given back by a
function whose body returns what it is given, ``fg_Data(Values)``, or by a
member function of it whose body returns what is in its object,
``Local.f_Get()`` of ``int *f_Get() { return &m_Value; }``, also moved
along it, ``return _pValue + 1;``, also
through a local reference to it, also through any number of them, also one bound
to a temporary that it extends, ``int const &Value = 42``, or either of a
conditional, ``[pValue = bFirst ? &First : &Second]``, each of which is
checked, also through a conditional reference, ``int *&pAlias = bFirst ? pFirst
: pSecond``, of what the pointers point to where it is captured, also the first value
of ``pValue ?: &g_Value``, which is its condition (the one declared last,
which goes first; of a constant condition, only the one it chooses), or what a changed
pointer was last assigned where it is captured, ``pValue = &Value; [pValue]``,
also moved, ``[pCopy = fg_Move(pValue)]``, borrows the local the
same way, and so does a pointer to a local captured by reference whose value
is copied before a suspension, ``int *pSaved = pValue`` in ``[&pValue]``, and is checked like ``[&Value]``, with the uses of the copy, and so
does a view captured by value, like ``[Span = CStrSpan{Buffer, 8}]``, also a
copy of one, ``[Copy]`` of ``CStrSpan Copy = Span``, and a
``std::initializer_list``, whose array lives as long as the local it
initializes, ``std::initializer_list<int> Values{1, 2}``, also an init capture,
``[Values = {1, 2}]``, whose array lives with the closure it is made in, not a
copy of it, so a cleanup, whose closure its guard moves, or one given to run
later can't use it. A pointer into a temporary, ``[pValue =
CValue{}.f_Get()]``, which is gone at the end of the full-expression whether
or not the coroutine suspends, isn't reported, and a helper that points a
pointer it is given by reference at what isn't one of its parameters,
``fg_Reset(pValue)`` with ``_pValue = &g_Value``, isn't followed: the
pointer can still have what it had before. What a member function of a local returns into it is followed
through each override a virtual one can run.

.. code-block:: c++

  CStr Name;
  co_await
    (
      g_Dispatch / [&Name]() -> TCFuture<CStr>  // warning
      {
        co_return Name;
      }
    )
  ;

  co_await fg_CallSafe
    (
      [&Name]() -> TCFuture<CStr>  // warning
      {
        co_await fg_Timeout(1.0);
        co_return Name;
      }
    )
  ;

A lambda that is called directly, or given as the function to call to
``fg_CallSafe``, runs right away, where the variables are, until it first
suspends, so it may use them until then. Before the first suspension, the
variables may only be used as values: read as scalars, copied or moved into
values that are not views, or given to functions that return a ``TCFuture``,
whose coroutines are held to the same rules. Any other use, like binding a
reference to it, taking its address, making a view, an awaiter or another
object from it, or giving it by reference to another function, counts as a use
after the suspension, since what it makes can keep a reference past it. Objects
returned by value are values, unless they are awaited. Handles, like
``TCActor`` and ``TCPromise``, are copied by what they are given to, and so is
what is given to them, like the arguments of an actor call. When it is made in
another lambda, that lambda must keep the variables too: be called directly and
not be a coroutine, or one that makes it before it first suspends, or hold a
copy of them. One that is dispatched, stored or
given to anything else can run after the scope of the variables has ended, even
before it suspends, so it is reported for capturing them at all. What the first
``co_await`` awaits runs before it suspends; a use in a lambda the coroutine
makes counts as after a suspension (but not one called where it is made, ``[&Value] { return Value; }()``,
that isn't a coroutine itself, which runs there). An awaiter whose ``await_ready`` only returns true, like that of ``co_await
g_AccessCoroutine``, doesn't suspend, unless it is virtual, which an
override can make suspend, and neither does one whose ``await_suspend`` only
returns false, which resumes right away.

A reference init capture of what has static storage, like ``[&Value =
s_Value]``, also of a member or an element of it, ``[&Value =
g_Data.m_Value]``, also through a cast to a reference, ``[&Value =
static_cast<int &>(g_Value)]``, refers to what lives as long as the program, and is fine, but not
one of ``thread_local`` storage, which is gone when the thread that made the
lambda ends, and neither is a pointer to one, ``[pData = &t_Data]``.

A lambda in a template, also a generic lambda, ``[&Value](auto _Future) ->
decltype(_Future)``, is checked in the instantiations of its call operator,
which tell what it returns and whether ``t_CType Copy = Value`` copies or
refers; one that isn't instantiated isn't checked.

A lambda that is dispatched and joined with ``f_CallSync()`` isn't kept that
way: what is between the dispatch and the join can leave the function first,
so, like a coroutine given references, it must run to completion (see
:doc:`malterlib-runs-to-completion <runs-to-completion>`), or return a
``TCUnsafeFuture`` when what it refers to is known to outlive it. A lambda
given only to a test category, like ``DMibTestCategory("Name") % [&] () ->
TCFuture<void> { ... }``, which dispatches it and waits for it with nothing
in between, may capture the locals of the function by reference.

Capture by value instead. ``TCUnsafeFuture`` lambdas opt out of the lifetime
protections and may capture by reference, and so may the cleanup functions of
``fg_AsyncDestroy``, which :doc:`malterlib-async-destroy <async-destroy>`
checks.

A lambda that code that runs to completion (see
:doc:`malterlib-runs-to-completion <runs-to-completion>`) starts and joins may
capture by reference what lives until the join: with an ``fg_AsyncDestroy``
guard that awaits its future, the locals declared before the guard in a scope
around it, when the guard is awaited early in their scope on every path, like
``co_await fg_Move(Guard)`` (the cleanup is joined only after the locals of
the coroutine are gone otherwise); awaited in its full-expression, the locals of the function; and
the parameters, of which references and pointers only of a function that runs
to completion. A pointer captured by reference, like ``[&pValue]``, also needs
what it is made from or pointed at, like ``pValue = &Inner``, also cast,
``static_cast<int *>(&Inner)``, or exchanged, ``fg_Exchange(pValue, &Inner)``,
to live until
the join, and so does a view, like ``[&Span]``, what it is made of or
assigned, like ``Span = {Local, 5}``.

A lambda whose introducer comes from a macro, like the ``[&] ()`` of
``DMibTestCategory("Name") % [&] () { ... }``, is reported where the macro is
used, in the code that writes the rest of the lambda.

A pointer to a local held inside another object that a lambda captures by
copy, like ``[Copy]`` of ``CHolder Copy{&Value}``, or an array of them, is not
followed, and neither is the address of a reference parameter, ``[pValue =
&_Value]``, which refers to what its caller keeps, by rules like those of
``fg_AsyncDestroy`` that the check can't see.
