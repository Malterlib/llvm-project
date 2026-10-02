.. title:: clang-tidy - malterlib-coroutine-lambda-call

malterlib-coroutine-lambda-call
===============================

Finds ``TCFuture`` lambda coroutines that use their captures after a
suspension and are called directly, or whose closures are given to something
that can call them directly. The coroutine refers to its captures through the
closure object, which is often a temporary that is gone after the first
suspension.

.. example-before
   TCFuture<void> fg_Timeout(double _Seconds);
   TCFuture<int> fg_Example(int Value)
   {

.. example-after
     co_return 0;
   }

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

A closure in a local of static storage duration that captures only copies,
``static const auto s_fRetry = [nLimit = fg_GetRetryLimit()](int _nAttempt) ->
TCFuture<bool> { ... };``, is never destroyed, so calling it directly isn't
reported; a copy of it in an automatic local is followed like any other
closure, and one that captures by reference is reported as before. A name of
a constant that isn't an odr-use, ``c_Size`` of ``constexpr int c_Size =
4096;`` read as a value, is no use of a capture.

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
in one that a constant condition never takes; and of a local view the result
is made from, not what an assignment of all of it, ``Result =
CStrSpan{nullptr, 0}``, replaces, nor what a return that never runs gives,
like after another ``co_return``), also ``std::addressof(Value)`` (but not the address of a static
member, ``&Data.ms_Value``), an array of a capture, or a pointer that a member function of a
capture gives, or the address of what one returns a reference to,
``&Value.f_Get()``, also an overloaded operator, ``&Values[0]`` (not one whose
body can be seen to return what isn't in the object, like ``return "Name";``,
also through a local that is not changed, ``int const *pValue = &g_Value;
return pValue;``, or what a pointer of a view holds, ``return mp_pStr;`` of
``CStrSpan::f_GetStr()``, which is what the view refers to, also plus a
number, ``return m_pStr + m_Length;``, also forwarded from the getter of a
base, ``return CImp::f_GetStr();`` of ``CStrPtr``, or only the branch a
constant condition selects, ``t_bDefault ? "Default" : m_Name``, but not a
static local it assigns what is in it, ``s_pValue = &m_Value``), a reference member of an aggregate bound to a capture or a
part of it, ``CRef{Value.m_Value}``, also made with parentheses, ``CRef(Value)``, the address of the copy of the closure
that an explicit object parameter taken by value is, ``&_Self``, what a
shared or unique owner it captures owns, unless the result holds by value a
keeper of it, or, of a use that keeps it before a suspension, the frame of
the call holds one in scope at the use, ``auto pKeep = pJob; auto *pValue =
&pJob->m_Value;`` (the copies themselves are the closure's, which no keeper
keeps), by type as reference capture tells
(:doc:`coroutine-reference-capture`), an owner of what is borrowed, made in
place, ``co_return CReadResult{pPage, CStrSpan(pPage->m_Text, 64)}``, also of
a lambda called directly that doesn't suspend, whose closure the result
outlives, ``co_return CReadResult{pJob, &pJob->m_Value}``, or of the
result's type, ``co_return fg_Move(Result)`` or ``co_return Result`` of a
local ``CReadResult``, which moves it, however
it was given the owner (a keeper that owns another object of a fitting type
isn't reported where the provenance can't tell the two owners apart, a
parameter or what a body that can't be seen gives; two locals made each of
its own, ``[pJob, pOther]() -> TCFuture<CReadResult> { co_return
CReadResult{pOther, &pJob->m_Value}; }``, are reported; an owner a capture
holds replaced in the closure's body
before the result is made, ``pPage = fg_ReadPage(); co_return
CReadResult{pPage, Text};``, is), and a
copy of a ``sharedbuffer`` owner, a ``CStr``, which is a value of its own,
``Result.m_Branch = Branch``,
and of a parameter captured by copy, a result made by a constructor that keeps the address of a
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
helper that gives back what it is given, ``&fg_Const(_Value)``, or a member
function it calls that keeps it in the object, ``f_Set(_pStr)``, what a
function whose body can't be seen makes of a capture it is given, ``co_return
fg_SpanOf(Text)`` (not of a copy of a number, ``fg_Name(Value)``),
but not in a local of the constructor, (also the conversion ``co_return`` makes into a
result of another type, which is judged by type, since the promise selects it and is never
looked into: a result that can hold a pointer, a reference or a view can keep a capture
given as an lvalue, ``co_return Value;``, whichever constructor would be selected, not one
given as an rvalue, ``co_return fg_Move(Value);``),
or the object a ``[*this]``
capture copies, also through a local or a view made of it (but not a number
computed from it, or a value copied from what such a pointer points to, like
``co_return *pValue``), is gone after that,
and is reported for these too, in a template in its instantiations, where a
getter of a capture of a dependent type is known, also a virtual one called
on a capture that is a value, whose type is what it is, ``Value.f_Get()``. A local record that isn't a
view returned by name carries what is put in each of its members: an address
assigned into it, ``CResult Result{}; Result.m_pValue = &Value; co_return
Result;``, also one it is made with, is reported as ``co_return
CResult{&Value};`` is. A lambda held in a variable is
reported too (also through any number of local copies of it, each followed to
what it is given to), and so is a closure that is given to anything else, like a
``TCFunction``, a function that is not in `SafeCallees`, or the caller, since
that can call it directly. A closure moved into a shared pointer with
``fg_ToSharedPointer`` is fine when that pointer is only given to
``fg_CallSafe``, directly or through a local. A function wrapper that holds
the closure, like ``TCFunctionMovable<…>(Lambda)`` or a parameter of that
type, is followed like the closure itself, and so is a local one at each
use the definition that gives it the closure reaches: its initializer, an
assignment, ``fRead = [...]``, also through a local reference bound to it,
and a local moved or copied from it at a use of it the closure reaches. Each
assignment and ``f_Clear()`` of it ends what it held: a use that only other
values reach, a call before the closure is assigned or after it is replaced,
cleared or assigned ``nullptr``, isn't the closure's, and a closure replaced
or cleared before any use is destroyed without running, which is nothing.
Calling it, like ``fFunction()``, is a direct call, and a const member
function of it whose result can't hold it, a presence check like ``if
(fRead)`` or ``fRead.f_IsEmpty()``, is nothing. Where a closure that refers
to the wrapper gives it a value or clears it, which closure a use sees can't
be told: one that holds only the closure over its scope is followed, and
nothing is said of another, a known miss. A lambda that captures the closure or its local, by reference or
by copy, and is only called in the scope, ``[&] { ... }()`` or
``fg_CallSafe([&fRead] { return fRead(); })``, runs there: its capture is
nothing, and the uses in its body are followed. ``f_Bind``
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
that return a ``TCFuture``, whose coroutines are held to the same rules. Any other use counts as a use after the suspension where what it makes keeps it
past it, told by the same rules as what a use does to the storage (below): kept
beyond the frame, in a member, a global, the result or a closure that runs
later, or held in a name of the frame, a local reference or pointer, a binding
or a capture of a closure that isn't called where it is made, that reads,
writes, gives or keeps it, wherever that name is. The address of a reference
follows the rules of a pointer: kept in a local that is only compared, ``CJob
*pLocal = &_Job; co_return pLocal == nullptr;``, or converted to an integer,
``reinterpret_cast<umint>(&_Job) != 0``, it touches nothing; an integer made of
it is its value, compared, tested, logged or returned as a number it touches
nothing, and converted back to a pointer it is the pointer again, ``auto
Address = reinterpret_cast<umint>(&_Value); co_await ...; co_return
*reinterpret_cast<int *>(Address);`` reads it. What is put in a
record held by value in the frame that isn't a view, an owner nor an actor is
carried by the member it is put in, by an aggregate, an assignment to the
member or a copy or move assignment the compiler defines: a later load of
that member, also through a getter whose body is seen, a copy the compiler
defines or a structured binding, is it again, and a load of another member,
the record's own storage, isn't; what a constructor or a member function
keeps of it (Rule R) is in the record as a whole. Such a record uses it where it is used or destroyed: a
call on it, a destructor that isn't trivial (seen, its body; not seen, it
uses what the record holds). One trivially destroyed and never used again
uses nothing of it, and so is an object returned by value. An awaiter
keeps what it holds while the coroutine is suspended; one that can't hold a
pointer, a reference or a view keeps nothing. A copy or a move assignment the compiler defines copies as the copy
or move constructor does, member by member: ``Info.m_User = User`` is
``CInfo Info{User}``. What a function or a constructor keeps of what it is
given is told by its body, before the first suspension too; of one whose body
can't be seen, also through a pointer to a function or a member function or
any other callee, by its function type, only its result, the object it makes
or is called on by a member function that isn't const (a const call operator
of a function object keeps nothing in it), and what its other arguments given
by reference or pointer to what isn't const refer to keep it, each when its
type can hold a pointer, a reference or a view, not when it holds
only values and owners, ``co_return COpaque(fg_Move(Actor));`` or ``CGuard
Guard(Value);`` of a class without members, and never an owner whose copy
keeps what it owns (``shared``, ``sharedbuffer``), a weak owner or a handle
given by reference, which it copies, ``co_return CConnection(fg_Move(Actor))``
of ``CConnection(TCActor<CConnectionActor> const &)`` also when it holds a
``void *``; a body that can be seen tells what it keeps of an owner given by
reference too, ``m_Text(_pPage->m_Text, 64)`` a part of what it owns. A
constructor whose parameter states that it keeps it (``DMibKeepsArgument``)
keeps it in the object it makes as a whole, whatever its members are, which
uses it where it is used or destroyed: ``CGuard Guard(Value);`` of
``CGuard(DMibKeepsArgument int const &)`` with ``~CGuard()`` is reported. A function that returns a ``TCFuture`` keeps nothing. What a body
that can't be seen keeps elsewhere, a static or a registry, isn't reported, a
known miss, unless its parameter states that it keeps it
(``DMibKeepsArgument``): ``fg_SaveActor(&Actor)`` of ``void
fg_SaveActor(DMibKeepsArgument TCActor<> const *_pActor)`` keeps the capture
past the call, reported. A result is judged by the type the promise stores, the ``T`` of
the ``TCFuture<T>``, not by what ``co_return`` gives it: one that can't hold a
pointer, a reference or a view keeps nothing; one the promise makes from what
it is given, of another type, in a forwarding ``return_value``, keeps it by
its type, as a constructor that can't be seen. What a lambda coroutine's
result keeps of what its captured owners own is alive when the result holds
by value a keeper of it, by type as reference capture tells
(:doc:`coroutine-reference-capture`), of an anchor of the borrow, every
object type on its derivation, the result expression and the whole
expression where the result is given what it holds, so of what an owner
owns where it is dereferenced, ``Result.m_pValue = &pPage->m_Value;``; the
captures themselves live in the closure, which no keeper keeps. What a
lambda coroutine captures by copy is, in its body, what it copies where the
closure is made, so a keeper made of one capture keeps only what that
capture's own value owns: ``CReadResult{pOther, CStrSpan(pPage->m_Text,
64)}`` of ``[pPage, pOther]``, two owners each given its own
``fg_ReadPage()``, is reported, also where a constructor of the result whose
body can be seen makes the view of one and keeps the other,
``CReadOther(pPage, pOther)``. A view the result is made of from a temporary
of the body, by a conversion that keeps it by reference (a constructor of the
view that takes it by reference, whose body can't be seen or keeps it), refers
to what is gone once the coroutine returns, however the lambda is run:
``co_return CText(Text);`` into a ``TCFuture<CStrSpan>`` made by
``TCStrSpan(CText const &)`` is reported at the ``co_return``. Six misses are known: a keeper that owns
a different object of a fitting type whose value can't be told (two
parameters), a member of a record read through a pointer to the record,
``CPlain *pResult = &Result; co_return pResult->m_pValue;``, a record that is a member of the object given the address of a
capture, ``m_Record.m_pValue = &Value; co_return m_Record;``, a record
captured by value given the address of another capture, ``Record.m_pValue =
&Value; co_return Record;``, an awaiter whose type can't hold a pointer, a
reference or a view (list its type in ``ViewTypes`` to have it checked), and
what a body that can't be seen keeps elsewhere, aren't reported. An owner
released or reassigned between the borrow and the result, a member of a
record set through a pointer to the record, and a parameter of the lambda
held by value given the address of a capture are reported. Handles, like ``TCActor`` and ``TCPromise``, are copied by what they
are given to, and so is what is given to them, like the arguments of an actor
call. What ``co_return`` gives outlives the call, so it must be a value too,
also in a coroutine that doesn't suspend, and so must the result it is stored
as: ``co_return &Value``, and ``co_return Text`` of a view made of a capture
into a ``TCFuture<CStrSpan>``, are reported. A view the lambda captures by
copy, a copy of it, ``co_return Text;``, and a pointer it holds or a getter of
it gives, ``co_return Text.f_GetStr();``, point where the view does, not into
the closure, so they are values (unless it is an init capture of a temporary,
``[Values = {1, 2}]``, whose array is a temporary of the full expression
that makes the closure); what a function or a
constructor whose body can't be seen makes of a capture,
``CStrSpan Span = fg_SpanOf(Text);``, can refer to it. A template is checked as each of its instantiations makes it,
and one that isn't instantiated isn't checked. Of a generic lambda, a call
of its call operator is checked by the specialization it runs,
``fTask.operator()<false>()`` by the one whose ``if constexpr
(t_bAsync)`` leaves out the suspension, and what else its closure is given
to by every specialization, any of which that can run. A direct call is reported only
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

Where what the result keeps can't be told, past what provenance follows, only
what can give it what refers to a capture counts, its storage given on as it
is or a value that can hold a pointer, not a number given by value,
``fg_GetName(g_aNames, iName)``, nor a condition, ``bUseCache ?
fg_CachedName() : ...``.

A local record returned by name carries what is put in its members, and a
copy of a record the closure holds by value, which the compiler makes member
by member, ``CPlain Copy = Record; co_return Copy;``, refers to what each
member of it was given: of a capture ``[Record]`` made with ``CPlain
Record{&g_Value}``, to static storage, which isn't reported; of a record given
the address of a capture, ``CPlain Local{&Value}; CPlain Copy = Local;``, to
the closure, which is:

.. example-before
   struct CResult { int const *m_pValue; };
   TCFuture<void> fg_Example()
   {

.. example-after
     co_return {};
   }

.. code-block:: c++

  co_await fg_CallSafe  // warning
    (
      [Value = 1]() -> TCFuture<CResult>
      {
        CResult Result{};
        Result.m_pValue = &Value;
        co_return Result;
      }
    )
  ;

Nothing is reported in a translation unit with an error that stops it from
compiling: its control flow isn't built, and what the errors leave can't be
relied on.

.. example-check

  struct CPage
  {
    int m_Value = 0;
  };
  struct CResult
  {
    TCSharedPointer<CPage> m_pPage;
    int const *m_pValue;
  };
  struct CPlain
  {
    int const *m_pValue;
  };
  struct CIdAwaiter
  {
    CIdAwaiter(int const *_pValue);
    bool await_ready();
    void await_suspend(std::coroutine_handle<> _Handle);
    int await_resume();
    unsigned long m_Id;
  };
  int g_Value;
  TCSharedPointer<CPage> fg_ReadPage();
  void fg_Register(int const *_pValue);
  TCFuture<void> fg_Timeout(double _Seconds);
  TCFuture<void> fg_Direct(int _Value)
  {
    co_await [=]() -> TCFuture<int> // warning: called directly, uses a capture after a suspension
      {
        co_await fg_Timeout(1.0);
        co_return _Value;
      }
      ()
    ;
    co_await [=]() -> TCFuture<int> // fine: uses its captures only before its first suspension
      {
        int Copy = _Value;
        co_await fg_Timeout(1.0);
        co_return Copy;
      }
      ()
    ;
    co_await []() -> TCFuture<int> // fine: no captures
      {
        co_await fg_Timeout(1.0);
        co_return 0;
      }
      ()
    ;
    co_return {};
  }
  TCFuture<void> fg_Released(TCSharedPointer<CPage> _pPage)
  {
    co_await fg_CallSafe // warning: the owner is replaced between the borrow and the result
      (
        [pPage = _pPage]() mutable -> TCFuture<CResult>
        {
          int const *pValue = &pPage->m_Value;
          pPage = fg_ReadPage();
          co_return CResult{pPage, pValue};
        }
      )
    ;
    co_await fg_CallSafe // fine: the result keeps the owner of what it borrows
      (
        [pPage = _pPage]() -> TCFuture<CResult>
        {
          co_return CResult{pPage, &pPage->m_Value};
        }
      )
    ;
    co_await fg_CallSafe // clean: a known miss, a keeper of another object of a fitting type
      (
        [pFirst = _pPage, pSecond = fg_ReadPage()]() -> TCFuture<CResult>
        {
          co_return CResult{pFirst, &pSecond->m_Value};
        }
      )
    ;
    co_return {};
  }
  TCFuture<void> fg_Records()
  {
    co_await fg_CallSafe // warning: a member of a record set through a pointer to the record
      (
        [Value = 1]() -> TCFuture<CPlain>
        {
          CPlain Result{};
          CPlain *pResult = &Result;
          pResult->m_pValue = &Value;
          co_return Result;
        }
      )
    ;
    co_await fg_CallSafe // warning: a parameter held by value given the address of a capture
      (
        [Value = 1](CPlain _Record) -> TCFuture<CPlain>
        {
          _Record.m_pValue = &Value;
          co_return _Record;
        },
        CPlain{}
      )
    ;
    CPlain Record{&g_Value};
    co_await fg_CallSafe // fine: a copy of a captured record whose member points to static storage
      (
        [Record]() -> TCFuture<CPlain>
        {
          CPlain Copy = Record;
          co_return Copy;
        }
      )
    ;
    co_await fg_CallSafe // warning: a copy of a record given the address of a capture
      (
        [Value = 1]() -> TCFuture<CPlain>
        {
          CPlain Local{&Value};
          CPlain Copy = Local;
          co_return Copy;
        }
      )
    ;
    co_await fg_CallSafe // clean: a known miss, a member read through a pointer to the record
      (
        [Value = 1]() -> TCFuture<int const *>
        {
          CPlain Result{&Value};
          CPlain *pResult = &Result;
          co_return pResult->m_pValue;
        }
      )
    ;
    co_await fg_CallSafe // clean: a known miss, a record captured by value
      (
        [Value = 1, Record = CPlain{}]() mutable -> TCFuture<CPlain>
        {
          Record.m_pValue = &Value;
          co_return Record;
        }
      )
    ;
    co_await fg_CallSafe // clean: a known miss, what a body that can't be seen keeps elsewhere
      (
        [Value = 1]() -> TCFuture<void>
        {
          fg_Register(&Value);
          co_return {};
        }
      )
    ;
    co_await [Value = 1]() -> TCFuture<int> // clean: a known miss, an awaiter whose type can't hold a pointer
      {
        co_return co_await CIdAwaiter(&Value);
      }
      ()
    ;
    co_return {};
  }
  struct CApp : CActor
  {
    CPlain m_Record;
    TCFuture<void> f_Member()
    {
      co_await fg_CallSafe // clean: a known miss, a record that is a member of the object
        (
          [this, Value = 1]() -> TCFuture<CPlain>
          {
            m_Record.m_pValue = &Value;
            co_return m_Record;
          }
        )
      ;
      co_return {};
    }
  };

Options
-------

.. option:: SafeCallees

   Semicolon-separated list of the functions, member functions and types that
   keep a closure alive while they run it, which the closures may be given
   to. Calls of ``operator /``, like ``g_Dispatch / Lambda``, and of ``operator
   >``, like ``Future > Lambda``, are always safe, except of a scope exit,
   ``g_OnScopeExit / Lambda``, also ``g_OnScopeExitWithException``, which calls
   it and then destroys it. None are built in; the Malterlib options file (:doc:`/clang-tidy/MalterlibOptionsFile`) adds Malterlib's, like `fg_CallSafe`, `fg_Dispatch` and `TCActorFunctor`, for every check that looks at what a closure is given to.
