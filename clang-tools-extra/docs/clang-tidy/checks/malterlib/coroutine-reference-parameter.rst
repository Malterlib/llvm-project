.. title:: clang-tidy - malterlib-coroutine-reference-parameter

malterlib-coroutine-reference-parameter
=======================================

Finds parameters of ``TCFuture`` coroutines that refer to data the caller
owns, like references, pointers, spans and string views, and that are used
after a suspension, when the data can be gone (not what a view taken by value
has of its own, which is in the frame, like its length, ``_Text.m_Length``,
also updated, ``_Text.m_Length = 0``, or a const member function of it that
gives only that, ``_Text.f_GetLen()``). What a lambda called where it is made
returns, ``co_return [&_Value] { return &_Value; }();``, is the result.
This is a best effort check.

.. code-block:: c++

  TCFuture<void> fg_Send(CStrSpan _Data)  // warning
  {
    co_await fg_Timeout(1.0);
    co_return co_await fg_Write(_Data);
  }

  TCFuture<void> fg_Send(CStr const &_Data)  // fine
  {
    co_return co_await fg_Write(CStr(_Data));
  }

A coroutine runs in its call until it first suspends, while the caller has the
data, so it may use such parameters until then. An awaiter whose ``await_ready`` only returns true, like that of ``co_await
g_AccessCoroutine``, doesn't suspend, unless it is virtual, which an
override can make suspend, also a static one, and neither does one whose
``await_suspend`` only returns false, which resumes right away. Naming a static
member through one, ``_pState->ms_Value`` or ``_pState->fs_Get()``, doesn't use
the data. Before the first suspension,
the data may only be used as values: read as scalars, copied or moved into
values that are not views, or given to functions that return a ``TCFuture``,
whose coroutines are held to the same rules. Any other use, like binding a
reference to it, taking its address, making a view, an awaiter or another
object from it, or giving it by reference to another function, counts as a use
after the suspension, since what it makes can keep a reference past it. Objects
returned by value are values, unless they are awaited. Handles, like
``TCActor`` and ``TCPromise``, are copied by what they are given to, and so is
what is given to them, like the arguments of an actor call. What the first
``co_await`` awaits runs before it suspends; a use in a lambda the coroutine
makes counts as after a suspension (but not one called where it is made, ``[&Value] { return Value; }()``, also
through its call operator by name, ``.operator()()``, or ``std::invoke``,
that isn't a coroutine itself, which runs there), and so does the object of a member
function whose arguments suspend, ``_Value.f_Read(co_await fg_Work())``, also
through what a pointer to it points to, ``(*_pValue).f_Read(...)``
(not by an await that a constant condition skips, ``false ? co_await
fg_Work() : 1`` or ``true || (co_await fg_Work(), true)``),
also through a pointer to a member function, ``(this->*fRead)(...)``, which
the function uses after them, and what an argument given by reference or
pointer refers to, ``fg_Read(_Value, co_await fg_Work())``, and what a pointer
points to accessed after an await in the same expression, ``_pValue[(co_await
fg_Work(), 0)]``, also through ``->`` or a cast of the pointer,
``(_pValue + co_await fg_Offset())->m_Value``, also through a
cast to a reference or ``fg_Move(_Value)``, also through a
pointer to a function, also when what
suspends is what initializes the captures of a lambda given to it. What ``co_return`` gives outlives the
call, so it must be a value too, also in a coroutine that doesn't suspend,
where only what the result is made from matters, also through a local, ``int
*pValue = &_Value; co_return pValue;`` (not when it is assigned something else
before each ``co_return`` of it, ``pValue = nullptr``, also in each branch of
an ``if`` that ends assigning it, but still when what can change it otherwise
comes after, ``fg_Set(pValue, _Value)``), also through any number of copies of
it or as either of a conditional, ``(bFirst ? pFirst : pSecond) = &_Value``,
not a pointer only read through,
``int Copy = *pValue; co_return nullptr;`` (what is read as a scalar on the
way, ``_Value ? g_pData : nullptr``, is a value), and
so must the result it is stored as, also through other locals and the elements
of arrays, what a reference member is bound to, also through a conditional,
``CResult{bChoose ? _Value : g_Value}``, a comma, an assignment, an increment
or a cast, what a function whose body can be seen gives back of it,
``CResult{fg_Const(_Value)}``, and the value a constant condition selects, ``false ?
CResult{_Value} : CResult{g_Value}``, and a copy of a view kept in it, like ``CResult{_Text}``, also of a
local copy of it, ``auto Copy = _Text;`` (but not
what a constructor that can be looked into, also through the constructor of a
base or one it delegates to that does, only copies into members that hold
values, ``m_Value(_Value)``, also of a trivially copyable class of values,
``CState m_State``, or reads as a number in its body, ``m_Value =
_Value;``, also through a local pointer or reference of it, ``m_Value =
*pValue`` of ``int const *pValue = &_Value``, or keeps it only in a branch a
constant condition never takes, ``if (t_bKeep) m_pValue = &_Value;``, nor a
``co_return`` in a discarded branch of an
``if constexpr``). A pointer parameter that a statement of the body points at other
data, like ``_pValue = &Local``, also with a condition that only chooses what,
``_pValue = _pValue ? &g_First : &g_Second``, also each branch of an ``if`` that ends
assigning it, ``if (_bChoose) _pValue = &g_First; else _pValue = &g_Second;``,
and nothing else assigns, also through a cast
to a reference, ``fg_Reset(static_cast<int *&>(_pValue))``, refers to that data
after it, and so does a view parameter assigned another view, ``_Values =
std::span<int>(Local)``, and assigning it anywhere, ``if (_bClear) _pValue = nullptr;``,
changes the pointer, not what it points to. Take the data by value to use it
later, or return a ``TCUnsafeFuture`` when the data is known to outlive the
coroutine.

A template is checked as each of its instantiations in the translation unit
makes it, with the types it has there, like ``f_Read<int *>``, also the
parameters a pack expands to and a class whose base is an actor in it; one
that isn't instantiated isn't checked.

Member functions of actors that are not static can't take references, whatever
their uses: actor calls move the arguments into the coroutine frame once, so
they are reported for any reference parameter, but not for an explicit
object, ``this CApp &_Self``, which is the object, like ``this``. Neither can they use pointers
and views they take, also before they first suspend, also what a template
parameter is instantiated with, `f_Read<int *>`: an actor call starts the
coroutine after the call returns, when what they point to can be gone.
Assigning a pointer parameter, ``_pValue = &m_Value``, doesn't use what it was
given, and neither does what reads it after it is assigned on every path.

A ``TCUnsafeFuture`` is a ``TCFuture``, and what its coroutine borrows is the
responsibility of the code that calls it, also when it gives the future on as
a ``TCFuture``, ``TCFuture<int> fg_Wrap(int &_Value) { return
fg_Unsafe(_Value); }``, which the check doesn't follow.

Pointers and references to classes that derive from ``CActor``,
``CActorInternal`` or ``CAllowUnsafeThis`` are used like ``this``, like the
``CInternal *_pThis`` of a static function, and are not reported.

A function that runs to completion, declared with ``DMibRunsToCompletion``
(see :doc:`malterlib-runs-to-completion <runs-to-completion>`), is joined by
its callers, which keep what its parameters refer to until then, so its
parameters are not reported, unless it is a member function of an actor.

Options
-------

.. option:: ViewTypes

   Semicolon-separated list of types and aliases that refer to data. A leading
   `*` matches the end of a name, a trailing one its start, and qualified
   names match qualified names. A template followed by a pattern of its first
   argument, `TCStr<CStrTraitsPtr_*>`, matches its specializations with such
   an argument, which is what a parameter whose type is deduced from an alias
   like `CStrPtr` is.
   Built in are `std::span;std::basic_string_view;std::initializer_list`, and the Malterlib options file (:doc:`/clang-tidy/MalterlibOptionsFile`) adds Malterlib's, like `*StrPtr` and `TCStr<CStrTraitsPtr_*>`, for every check that looks at views.

.. option:: CheckPointers

   Whether pointer parameters are reported. Default is `true`.
