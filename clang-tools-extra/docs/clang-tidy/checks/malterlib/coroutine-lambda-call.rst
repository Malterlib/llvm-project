.. title:: clang-tidy - malterlib-coroutine-lambda-call

malterlib-coroutine-lambda-call
===============================

Finds ``TCFuture`` lambda coroutines that use their captures after a
suspension and are called directly, or whose closures are given to something
that can call them directly. The coroutine refers to its captures through the
closure object, which is often a temporary that is gone after the first
suspension.

.. code-block:: c++

  co_await [=]() -> TCFuture<int>  // warning
    {
      co_await fg_Timeout(1.0);
      co_return Value;
    }
    ()
  ;

  co_await fg_CallSafe  // fine
    (
      [=]() -> TCFuture<int>
      {
        co_await fg_Timeout(1.0);
        co_return Value;
      }
    )
  ;

Run such lambdas through ``fg_CallSafe``, ``g_Dispatch``, ``self /`` or an
actor functor, which keep the closure alive while it runs; what its result
refers to of its captures, like ``co_return &Value`` into a ``TCFuture<int
const *>``, also of a member of it, through any number of local references
to it, either of a conditional, ``&(bChoose ? Value : g_Value)``, a cast,
a prefix increment, ``++Value``, or a function that
returns what it is given, ``&fg_Const(Value)``, or a constructor of the
result that keeps its address, also by ``std::addressof``, through a
conditional, ``bKeep ? &_Value : nullptr`` (only the value a constant
condition chooses), or a local pointer or reference of the
constructor, ``int const *pSaved = &_Value;``, the one the
conversion selects (but not in a discarded branch of an ``if constexpr``, nor
in one that a constant condition never takes; and of a local the result is
made from, not what an assignment of all of it, ``Result = CResult{nullptr}``,
replaces, nor what a return that never runs gives, like after another
``co_return``), also ``std::addressof(Value)`` (but not the address of a static
member, ``&Data.ms_Value``), an array of a capture, or a pointer that a member function of a
capture gives, or the address of what one returns a reference to,
``&Value.f_Get()``, also an overloaded operator, ``&Values[0]`` (not one whose
body can be seen to return what isn't in the object, like ``return "Name";``,
also through a local that is not changed, ``int const *pValue = &g_Value;
return pValue;``, but not a static local it assigns what is in it,
``s_pValue = &m_Value``), a reference member of an aggregate bound to a capture or a
part of it, ``CRef{Value.m_Value}``, also made with parentheses, ``CRef(Value)``, the address of the copy of the closure
that an explicit object parameter taken by value is, ``&_Self``, a result made by a constructor that keeps the address of a
capture it is given, in its initializers or its body (not where a constant
condition never runs, ``if (false)`` or ``false ? _pValue : nullptr``), also cast to another
pointer, ``m_pValue(static_cast<void const *>(&_Value))``, or binds a reference
member to what a pointer it is given points to, ``m_Value(*_pValue)``, or takes
the address of a member of it, ``m_pValue(&_pValue->m_Value)``, or through the
constructor it delegates to, ``CResult(int const &_Value) : CResult(&_Value)``,
or of a base, ``CResult(int const &_Value) : CBase(_Value)``, also braced,
``m_pValue{&_Value}``, and a reference member bound to either of a
conditional, ``m_Value(bChoose ? _Value : g_Value)``, also through a cast to a
reference, ``&static_cast<int const &>(_Value)``, or more constructors of bases
than can be looked into, or one whose body can't be seen, defined in another
file, other than a copy or a move, which can keep it, also through a
helper that gives back what it is given, ``&fg_Const(_Value)``,
but not in a local of the constructor, (also converting to the result, ``co_return Value;``),
what is assigned to parts of a returned local, or the object a ``[*this]``
capture copies, also through a local or a view made of it (but not a number
computed from it, or a value copied from what such a pointer points to, like
``co_return *pValue``), is gone after that,
and is reported for these too, in a template in its instantiations, where a
getter of a capture of a dependent type is known, also a virtual one called
on a capture that is a value, whose type is what it is, ``Value.f_Get()``. A lambda held in a variable is
reported too (also through any number of local copies of it, each followed to
what it is given to), and so is a closure that is given to anything else, like a
``TCFunction``, a function that is not in `SafeCallees`, or the caller, since
that can call it directly. A closure moved into a shared pointer with
``fg_ToSharedPointer`` is fine when that pointer is only given to
``fg_CallSafe``, directly or through a local. A function wrapper that holds
the closure, like ``TCFunctionMovable<…>(Lambda)``, a parameter of that type,
or a local one that is never given another function, is followed like the
closure itself: calling it, like ``fFunction()``, is a direct call. ``f_Bind``
and
``f_BindByValue`` keep it alive only when they bind
``CActor::f_DispatchWithReturn`` and get it first; another bound function,
or an argument of the dispatched function, gets it to keep or drop; so does a
function given to ``f_Invoke`` or ``f_DispatchWithReturn`` that is not their
first argument. The
result of a statement expression leaves the scope it is made in.

A direct call runs the coroutine until it first suspends, while the closure is
there, so a lambda that uses its captures only before its first suspension may
be called directly and go anywhere. What the first ``co_await`` awaits runs
before it suspends. A use in a lambda the coroutine makes counts as after a
suspension, since that lambda can be called at any time, unless the lambda
captures a copy. Before the first suspension, the copies the lambda captures,
and a copy of the object, ``[*this]``, may only be used as values: read as
scalars, copied or moved into values that are not views, or given to functions
that return a ``TCFuture``, whose coroutines are held to the same rules. Any
other use, like binding a reference to it, taking its address, making a view,
an awaiter or another object from it, or giving it by reference to another
function, counts as a use after the suspension, since what it makes can keep a
reference past it. Objects returned by value are values, unless they are
awaited. Handles, like ``TCActor`` and ``TCPromise``, are copied by what they
are given to, and so is what is given to them, like the arguments of an actor
call. What ``co_return`` gives outlives the call, so it must be a value too,
also in a coroutine that doesn't suspend, and so must the result it is stored
as: ``co_return &Value``, and ``co_return Text`` into a ``TCFuture<CStrSpan>``,
are reported. A template is checked as each of its instantiations makes it,
and one that isn't instantiated isn't checked. A direct call is reported only
of a temporary closure or one in a local variable: a closure given as a
parameter, ``_fFunction(1)``, or held elsewhere, in a member,
``State.m_Function()``, or through a pointer, ``(*pFunction)()``, like a
function wrapper holds it, is checked where it is given.

This replaces the runtime check of Malterlib's safe-check builds, which
reports such coroutines when they suspend.

Lambdas without captures, lambdas with an explicit object parameter taken by
value, whose copy of the closure lives in the coroutine frame (unless what
they return refers to it, also when called as ``Task.operator()()``), and
``TCUnsafeFuture`` lambdas are not reported.

A lambda whose introducer comes from a macro, like the ``[&] ()`` of
``DMibTestCategory("Name") % [&] () { ... }``, is reported where the macro is
used, in the code that writes the rest of the lambda.

Options
-------

.. option:: SafeCallees

   Semicolon-separated list of the functions, member functions and types that
   keep a closure alive while they run it, which the closures may be given
   to. Calls of ``operator /``, like ``g_Dispatch / Lambda``, and of ``operator
   >``, like ``Future > Lambda``, are always safe, except of a scope exit,
   ``g_OnScopeExit / Lambda``, also ``g_OnScopeExitWithException``, which calls
   it and then destroys it. None are built in; the Malterlib options file (:doc:`/clang-tidy/MalterlibOptionsFile`) adds Malterlib's, like `fg_CallSafe`, `fg_Dispatch` and `TCActorFunctor`, for every check that looks at what a closure is given to.
