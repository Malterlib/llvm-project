.. title:: clang-tidy - malterlib-actor-dispatch-this

malterlib-actor-dispatch-this
=============================

Finds lambdas that capture ``this`` of an actor and run on another actor:
``g_Dispatch(Actor) / Lambda``, with a blocking or concurrent actor checkout
too, ``g_OnScopeExitActor(Actor) / Lambda``, which dispatches it there when
the scope exit runs, and ``Actor / Lambda``, also with the operator called by
name, like
``g_Dispatch(Actor).operator/(Lambda)``. The object of an actor belongs to its own actor,
so using it from another one races with it.

.. code-block:: c++

  auto BlockingActorCheckout = fg_BlockingActor();
  co_await
    (
      g_Dispatch(BlockingActorCheckout) / [this]()  // warning
      {
        return m_Value + 1;
      }
    )
  ;

Capture copies of what the lambda needs instead. Lambdas run on the own actor,
with plain ``g_Dispatch /``, ``self /`` or ``fg_ThisActor(this) /``, also
through a weak handle that is locked again, like
``fg_ThisActor(this).f_Weak()`` and ``f_Lock()``, also either of a conditional
of them, ``(bChoose ? Own : OwnCopy) /``, are fine. Init captures of
``this``, like ``[pThis = this]``, are reported too, and so are pointers and
references into the object, like ``[pValue = &m_Value]`` or ``[&Value =
m_Value]``, also through any number of local pointers and references to it,
of a local pointer assigned more than once, the last assignment, a helper that
gives it back as it is, ``fg_Const(m_Value)`` (by what its body returns, when it
can be seen, so not one that gives back a global, and else by the types), ``std::addressof``
and structured bindings, ``auto &[Value] = m_State``, what a member function of
it gives a pointer or reference to, like ``mp_Items.f_FindEqual(Key)``, a copy
of a pointer through a reference to it, a pointer captured by reference that
is pointed into it anywhere, and a local pointer that
isn't pointed elsewhere before the lambda copies it (but not the value of a
pointer member, like
``[pInternal = mp_pInternal]``, which points to what isn't the object), and an
init capture of a pointer to an actor that is made of what can't be told,
``[pActor = fg_GetActor()]``, like a local of it. So are the cleanups of
subscriptions that run on another actor, ``g_ActorSubscription(Actor) /`` and
``g_BlockingActorSubscription /``, and timer callbacks given another actor,
like ``fg_RegisterTimer(Period, Lambda, Actor)`` (an empty actor, like ``{}``,
is the current one, where using the object of another actor it captures is
reported too), and lambdas given to
``fg_Dispatch(Actor, Lambda)`` or ``fg_CallSafeDispatchedOn(Actor, Lambda)``
with another actor, or to an actor functor made with one, ``fg_ActorFunctor(Actor,
Lambda)`` or ``TCActorFunctor<...>(Actor, Lambda)``, also a weak one,
``fg_ActorFunctorWeak`` or ``TCActorFunctorWeak``, or to
``fg_ConcurrentDispatch(Lambda)``, also through an
unchanged local pointer to one of them, also dereferenced, ``(*fDispatch)(...)``,
also either of a conditional of closures, ``bChoose ? fFirst : fSecond``, or
the one a constant condition chooses, or
``g_ConcurrentDispatch /``, and lambdas bound to run on another actor,
like ``Actor.f_Bind<&CActor::f_DispatchWithReturn<...>>(Lambda)``, or
called on it, ``Actor(&CActor::f_DispatchWithReturn<...>, Lambda)``, also by
the name of the operator, ``Actor.operator()(...)``, or through
``std::invoke(Actor, ...)``, also
of ``CActor::f_Dispatch``, which runs it too, and
through an unchanged local, ``auto fDispatch = &CActor::f_DispatchWithReturn<int>``.
A pointer to the object, or into it, given to the function to call with on
another actor, ``fg_Dispatch(Actor, Function, this)``, also through a local
alias, ``auto *pThis = this``, is reported too.

A static or free function that takes a pointer or reference to the object of
an actor, like ``static TCFuture<void> fs_Stop(CApp *_pThis)``, also by a
reference to a pointer, ``CApp *const &_pThis``, or a member
function of a class that is neither an actor nor a helper of one (deriving from
``CActorInternal`` or ``CAllowUnsafeThis``), like
``mp_Interface.f_Publish(Manager, this)``, but not a lambda, constructor or
destructor, uses it like
``this`` on the actor the call runs on, so it must be given the object of that
actor: ``this``, what it holds or owns, like its internals,
``fs_Clean(&*mp_pInternal)``, such a parameter of the function the call is in,
given on, ``fs_Stop(_pThis)``, an unchanged local alias of them, or null,
also as a default argument, which is what it is made of. Giving it the
object of another actor, like ``fs_Stop(m_pOther)``, or such a parameter that
is changed (given by non-const reference only changes it when the function, as
instantiated, assigns through it), is reported at the call, also of a template whose instantiation
takes the object, ``fg_Run<CApp>(CApp *_pActor)`` of ``t_CActor *_pActor``
(but not a function of a system header, nor one that only gives back what it
is given, like ``fg_Forward`` and ``fg_Move``, whose result is checked where it
is used, nor one that only constructs or destroys with it, like
``fg_DeleteObject`` and ``fg_ConstructObject``: deletes it, calls its
destructor, frees its memory through a function whose body can't be seen,
gives it to a constructor that only keeps it or does the same with it, or tests
it for null, which manages the object without using it; a function whose body
can be seen is looked into), also ``fg_ThisActor(pOther)``. So
in the function, also in
a template whose instantiations make it the object of an actor,
the parameter is the object of the current actor, like ``this`` in a member
function: ``self /``, ``g_Dispatch /``, ``_pThis->self /`` and
``fg_ThisActor(_pThis) /`` run lambdas that capture it on its actor, and
giving it to a function that runs on another actor is reported like ``this``.

A helper of an actor, a class that derives from ``CActorInternal`` or
``CAllowUnsafeThis`` (but isn't an actor), uses the object of the actor it is
constructed with like ``this``, so it must be constructed with the object of
the current actor: a pointer or reference to the object of an actor given to
its constructor, ``CInternal(this)``, or to ``fg_Construct`` whose result
constructs it, ``mp_pInternal(fg_Construct(this))``, also by a default
argument, ``CInternal(CApp *_pApp = g_pOther)``, reported where it is used, is
reported when it may be of another actor, like ``fg_Construct(m_pOther)``. Generic code that gives on
what it is given, like the constructor call of ``fg_ConstructObject``, is
checked where it is given.

A lambda that captures a pointer or reference to the object of an actor, like
the parameter ``CActor *_pThis`` of a static function or a free function, is
reported too when it runs on another actor than the actor of that object (not
on ``fg_ThisActor(_pThis)`` or a handle converted from ``_pThis->self``), also
a pointer into the object,
like ``[pValue = &_pThis->m_Value]``, and through local aliases, with what
they were last assigned where they are read, ``int *pCopy = pValue; pValue =
nullptr;``
(one pointed elsewhere after the handle is made from it may be of another
actor). In a
member function of an actor, or a function given the object of an actor, a
lambda that runs on the own actor is reported when it captures the object of
another actor. A pointer pointed into the
object through ``fg_Exchange``, swapped with a local that points into it,
``fg_Swap(pValue, pOther)``, or through references to it, ``int *&pAlias =
pValue``, counts too, but not the
address of a static member, like ``&this->mc_Value``.

A lambda made into a function object first, like ``TCFunction<int
()>{[this] { ... }}``, also held in a local and moved or copied, ``fg_Dispatch(Other,
fg_Move(fCallback))``, is checked too, with the lambda it was last assigned on
every path, ``fCallback = [this] { ... }``, or each it can have been
assigned before it is dispatched, or in a loop around it (the dispatch keeps a
copy, which what is assigned after doesn't change), and so is a local pointer assigned the
data of an actor, also made with it and assigned something else after a lambda
copied it, ``pValue = nullptr``, ``pValue = &_pActor->m_Value``, also an offset into an
array of it, ``_pActor->m_Values + 1``, the last of a comma, ``(fg_Log(),
&m_Value)``, what an assignment gives, ``(pValue = &m_Value)``, and each
value of a conditional, like
``bChoose ? _pFirst : _pSecond``, also ``_pFirst ?: _pSecond``, or the one a
constant condition chooses. A lambda whose introducer comes from a macro, like the ``[&] ()`` of
``DMibTestCategory("Name") % [&] () { ... }``, is reported where the macro is
used, in the code that writes the rest of the lambda.

A pointer into the object of an actor held inside another object that a lambda
captures by copy, like ``[Copy]`` of ``CHolder Copy{&m_Value}``, is not
followed.

Options
-------

.. option:: OtherActorDispatchers

   Semicolon-separated list of the types whose ``operator /`` runs a lambda on
   another actor, also of the classes derived from them, like a
   ``TCActorInterface`` of ``TCActor``. None are built in; the Malterlib options file (:doc:`/clang-tidy/MalterlibOptionsFile`) adds Malterlib's, like `CDispatchHelperWithActor` and `TCActor`.
